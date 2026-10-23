#include "glancelot/display_manager.hpp"
#include "monitor_customization_topology.hpp"

#include <QGuiApplication>
#include <QScreen>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QPointer>
#include <algorithm>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QFileInfo>
#include <QScopedValueRollback>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace glancelot {

static PresentationWindowFactory s_presentationWindowFactory = nullptr;
// 256 MiB of retained decoded pixels across current/pinned/comparison/previous/pending and the F040 reference;
// GPU textures are budgeted separately per canvas (see PresentationWindow).
static qint64 s_imageBudgetBytes = 256LL * 1024 * 1024;

void DisplayManager::setImageBudgetBytes(qint64 bytes) { s_imageBudgetBytes = std::max<qint64>(0, bytes); }
qint64 DisplayManager::imageBudgetBytes() { return s_imageBudgetBytes; }

namespace {
#ifdef Q_OS_WIN
// Windows adapter: map QScreen::name() (\\.\DISPLAYn) to the monitor device interface path
// (\\?\DISPLAY#DEL40F4#...) and return its hardware id segment ("DEL40F4"), which encodes the EDID
// manufacturer + product code. The per-port instance segment is deliberately not used.
// UNVERIFIED on real multi-monitor hardware.
QString platformMonitorCode(const QScreen* screen) {
    const std::wstring adapterName = screen->name().toStdWString();
    DISPLAY_DEVICEW adapter{}; adapter.cb = sizeof(adapter);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &adapter, 0); ++i) {
        if (adapterName != adapter.DeviceName) { adapter = {}; adapter.cb = sizeof(adapter); continue; }
        DISPLAY_DEVICEW mon{}; mon.cb = sizeof(mon);
        if (!EnumDisplayDevicesW(adapter.DeviceName, 0, &mon, 0x00000001 /*EDD_GET_DEVICE_INTERFACE_NAME*/)) return {};
        const QStringList parts = QString::fromWCharArray(mon.DeviceID).split(QLatin1Char('#'));
        return parts.size() > 2 ? parts[1] : QString();
    }
    return {};
}
#else
QString platformMonitorCode(const QScreen*) { return {}; }
#endif
} // namespace

void DisplayManager::setPresentationWindowFactory(PresentationWindowFactory factory) {
    s_presentationWindowFactory = std::move(factory);
}

QVariantMap DisplayInfo::toVariantMap() const {
    QVariantMap map;
    map[QStringLiteral("index")] = index;
    map[QStringLiteral("id")] = id;
    map[QStringLiteral("name")] = name;
    map[QStringLiteral("manufacturer")] = manufacturer;
    map[QStringLiteral("model")] = model;
    map[QStringLiteral("serialNumber")] = serialNumber;
    map[QStringLiteral("label")] = QStringLiteral("Display %1 · %2").arg(index+1).arg(name);
    map[QStringLiteral("x")] = geometry.x();
    map[QStringLiteral("y")] = geometry.y();
    map[QStringLiteral("width")] = geometry.width();
    map[QStringLiteral("height")] = geometry.height();
    map[QStringLiteral("devicePixelRatio")] = devicePixelRatio;
    map[QStringLiteral("refreshRate")] = refreshRate;
    map[QStringLiteral("colorDepth")] = colorDepth;
    map[QStringLiteral("colorSpace")] = colorSpace;
    map[QStringLiteral("isPrimary")] = isPrimary;
    map[QStringLiteral("isAvailable")] = isAvailable;
    return map;
}

struct DisplayManager::Private {
    DisplayManager* q;
    PresentationMode mode{PresentationMode::Mirror};
    bool active{false};
    QString activeId;
    QString activeName;

    QString currentPath;
    QImage currentImage;
    quint64 currentGeneration{0};
    quint64 displayedGeneration{};
    QString routeStatus{QStringLiteral("Output disabled")};
    QVariantMap routingPolicy;
    QString preferredId;
    QTimer cadence;
    QImage pendingImage;
    QString pendingPath,followFolder;
    quint64 pendingGeneration{};
    bool folderHeld{},publishingTimed{},pendingValid{};

    QString pinnedPath;
    QImage pinnedImage;
    quint64 pinnedGeneration{0};

    QString comparisonPath;
    QImage comparisonImage;
    quint64 comparisonGeneration{0};
    quint64 comparisonGenerationHighWater{0};

    QString previousPath;
    QImage previousImage;

    // F040 reference window. Its generation high-water rejects stale pins even after a clear.
    QString referencePath;
    QImage referenceImage;
    quint64 referenceGenerationHighWater{0};
    bool referenceProtected{false};   // while admitting an output pin: one explicit pin never evicts the other

    QHash<QString, QString> fitModes;           // per display id
    QHash<QString, QVariantMap> viewports;      // bounded session state by display identity
    std::function<QString(const QString&)> outputBindingProvider;
    std::function<QString()> toggleBindingProvider;

    std::unique_ptr<IPresentationWindow> presentationWindow;
    QPointer<QScreen> presentationScreen;

    explicit Private(DisplayManager* owner) : q(owner) {
        cadence.setInterval(3000);cadence.setSingleShot(true);
        QObject::connect(&cadence,&QTimer::timeout,q,[this]{
            if(!active||!pendingValid)return;
            const auto image=pendingImage;const auto path=pendingPath;const auto generation=pendingGeneration;
            pendingValid=false;pendingImage={};pendingPath.clear();publishingTimed=true;q->updateImage(path,image,generation);publishingTimed=false;
        });
    }
    void status(const QString& value){if(routeStatus==value)return;routeStatus=value;emit q->routingStatusChanged();}

    DisplayInfo screenToInfo(const QScreen* screen, int index) const {
        if (!screen) return {};
        DisplayInfo info;
        info.index = index;
        info.id = DisplayManager::computeDisplayId(screen);
        info.name = !screen->name().trimmed().isEmpty() ? screen->name().trimmed() : QStringLiteral("Display %1").arg(index + 1);
        info.manufacturer = screen->manufacturer().trimmed();
        info.model = screen->model().trimmed();
        info.serialNumber = screen->serialNumber().trimmed();
        info.geometry = screen->geometry();
        info.availableGeometry = screen->availableGeometry();
        info.physicalSizeMm = screen->physicalSize().toSize();
        info.devicePixelRatio = screen->devicePixelRatio();
        info.refreshRate = screen->refreshRate() > 0 ? screen->refreshRate() : 60.0;
        info.colorDepth = screen->depth() > 0 ? screen->depth() : 24;
        info.colorSpace = DisplayManager::colorSpaceName(screen);
        info.isPrimary = (screen == QGuiApplication::primaryScreen());
        info.isAvailable = true;
        return info;
    }

    mutable bool displaysDirty{true};
    mutable QList<DisplayInfo> cachedDisplays;
    void notifyDisplays(){displaysDirty=true;emit q->displaysChanged();}
    QList<DisplayInfo> enumerateDisplays() const {
        if(!displaysDirty)return cachedDisplays;
        QList<DisplayInfo> result;
        const auto screens = QGuiApplication::screens();
        result.reserve(screens.size());
        for (int i = 0; i < screens.size(); ++i) {
            result.append(screenToInfo(screens[i], i));
        }
        cachedDisplays=result;displaysDirty=false;return result;
    }

    QScreen* findScreenById(const QString& id) const {
        if (id.isEmpty()) return nullptr;
        for (QScreen* s : QGuiApplication::screens()) {
            if (DisplayManager::computeDisplayId(s) == id) return s;
        }
        return nullptr;
    }

    QImage secondaryImage() const {
        if (!comparisonImage.isNull()) return comparisonImage;
        return !pinnedImage.isNull() ? pinnedImage : previousImage;
    }

    qint64 retainedBytes() const {
        QSet<qint64> keys; qint64 total = 0;
        for (const QImage* im : {&currentImage, &pinnedImage, &comparisonImage, &previousImage, &pendingImage, &referenceImage}) {
            if (im->isNull() || keys.contains(im->cacheKey())) continue;
            keys.insert(im->cacheKey()); total += im->sizeInBytes();
        }
        return total;
    }

    // Shed in order: previous, pending, comparison, reference, pinned. The current image is never dropped. The
    // reference goes before the pinned output: losing it closes a local window with a notice, while losing the pin
    // silently switches what an audience display shows.
    void enforceBudget() {
        if (retainedBytes() <= s_imageBudgetBytes) return;
        previousImage = QImage(); previousPath.clear();
        if (retainedBytes() <= s_imageBudgetBytes) {
            applyImagesToWindow();
            return;
        }
        // Admit a latest frame after shedding optional history, but preserve the
        // displayed and pinned frames if both cannot fit at once.
        if(pendingValid){pendingValid=false;pendingImage={};pendingPath.clear();status("Output holds its last image: the latest frame exceeds the retained-image budget.");}
        if(retainedBytes()<=s_imageBudgetBytes){applyImagesToWindow();return;}
        comparisonImage = QImage(); comparisonPath.clear(); comparisonGeneration = 0;
        if (retainedBytes() <= s_imageBudgetBytes) {
            applyImagesToWindow();
            return;
        }
        if (!referenceImage.isNull() && !referenceProtected) {
            const auto shed=referencePath;
            referenceImage = QImage(); referencePath.clear();
            emit q->referenceShed(shed);
            emit q->referenceChanged();
            if (retainedBytes() <= s_imageBudgetBytes) { applyImagesToWindow(); return; }
        }
        if (!pinnedImage.isNull()) {
            pinnedImage = QImage(); pinnedPath.clear(); pinnedGeneration = 0;
            emit q->pinnedPathChanged(pinnedPath);
            if (mode == PresentationMode::Pinned) q->setMode(PresentationMode::Mirror);
        }
        applyImagesToWindow();
    }

    void applyImagesToWindow() {
        if (!presentationWindow) return;

        const bool isComp = (mode == PresentationMode::Comparison);
        presentationWindow->setComparisonMode(isComp);

        if (mode == PresentationMode::Pinned) {
            presentationWindow->setPrimaryImage(pinnedImage);
            presentationWindow->setSecondaryImage(QImage());
        } else if (mode == PresentationMode::Comparison) {
            presentationWindow->setPrimaryImage(currentImage);
            presentationWindow->setSecondaryImage(secondaryImage());
        } else {
            // Mirror
            presentationWindow->setPrimaryImage(currentImage);
            presentationWindow->setSecondaryImage(QImage());
        }
    }
};

DisplayManager::DisplayManager(QObject* parent)
    : QObject(parent), d(std::make_unique<Private>(this)) {
    auto observe=[this](QScreen* screen) {
        connect(screen,&QScreen::geometryChanged,this,[this]{d->notifyDisplays();});
        connect(screen,&QScreen::availableGeometryChanged,this,[this]{d->notifyDisplays();});
        connect(screen,&QScreen::logicalDotsPerInchChanged,this,[this]{d->notifyDisplays();});
        connect(screen,&QScreen::refreshRateChanged,this,[this]{d->notifyDisplays();});
    };
    for(auto* screen:QGuiApplication::screens())observe(screen);
    // Listen to screen topology changes
    if (qApp) {
        connect(qApp, &QGuiApplication::screenAdded, this, [this,observe](QScreen* screen) {
            observe(screen); d->notifyDisplays();
        });
        connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen* screen) {
            if (d->presentationScreen == screen) {
                closePresentation();
            }
            d->notifyDisplays();
        });
        connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen*) {
            d->notifyDisplays();
        });
    }
}

DisplayManager::~DisplayManager() {
    closePresentation();
}

QList<DisplayInfo> DisplayManager::displays() const {
    return d->enumerateDisplays();
}

QVariantList DisplayManager::displaysVariant() const {
    const auto list = d->enumerateDisplays();
    QVariantList varList;
    varList.reserve(list.size());
    for (const auto& item : list) {
        auto row=item.toVariantMap();
        row["identityConfidence"]=monitor_topology::strongIdentity(item,list)?"strong":"weak";
        row["canPosition"]=!QGuiApplication::platformName().startsWith("wayland");
        varList.append(row);
    }
    return varList;
}

int DisplayManager::displayCount() const {
    return QGuiApplication::screens().size();
}

DisplayInfo DisplayManager::displayForId(const QString& id) const {
    for (const auto& info : d->enumerateDisplays()) {
        if (info.id == id) return info;
    }
    return {};
}

DisplayInfo DisplayManager::preferredPresentationDisplay() const {
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) return {};

    // If multiple monitors, pick first non-primary screen
    if (screens.size() > 1) {
        const QScreen* primary = QGuiApplication::primaryScreen();
        for (int i = 0; i < screens.size(); ++i) {
            if (screens[i] != primary) {
                return d->screenToInfo(screens[i], i);
            }
        }
    }

    // Single screen fallback
    return d->screenToInfo(screens.first(), 0);
}

QStringList DisplayManager::assignDisplayIds(const QList<DisplayIdentitySource>& sources) {
    QStringList ids;
    QHash<QString, int> seen;
    for (const auto& s : sources) {
        const QString mfg = s.manufacturer.trimmed(), model = s.model.trimmed();
        const QString serial = s.serial.trimmed(), code = s.platformCode.trimmed();
        QString key;
        if (!serial.isEmpty() && (!mfg.isEmpty() || !model.isEmpty() || !code.isEmpty())) {
            key = QStringLiteral("serial:%1|%2|%3|%4").arg(mfg, model, code, serial);
        } else if (!mfg.isEmpty() || !model.isEmpty() || !code.isEmpty()) {
            key = QStringLiteral("model:%1|%2|%3|%4x%5").arg(mfg, model, code)
                      .arg(s.physicalSizeMm.width()).arg(s.physicalSizeMm.height());
        } else {
            // Last resort: no hardware naming at all. The Windows \\.\DISPLAYn name is NOT used
            // because it changes with topology; the ordinal separates identical panels.
            key = QStringLiteral("geom:%1x%2|%3x%4").arg(s.pixelSize.width()).arg(s.pixelSize.height())
                      .arg(s.physicalSizeMm.width()).arg(s.physicalSizeMm.height());
        }
        const int ordinal = seen[key]++;
        const QString identity = QStringLiteral("%1#%2").arg(key).arg(ordinal);
        ids.append(QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(16)));
    }
    return ids;
}

static DisplayIdentitySource identitySourceFor(const QScreen* s) {
    DisplayIdentitySource src;
    src.manufacturer = s->manufacturer();
    src.model = s->model();
    src.serial = s->serialNumber();
    src.platformCode = platformMonitorCode(s);
    src.physicalSizeMm = s->physicalSize().toSize();
    src.pixelSize = s->geometry().size();
    return src;
}

QString DisplayManager::computeDisplayId(const QScreen* screen) {
    if (!screen) return QStringLiteral("none");
    const auto screens = QGuiApplication::screens();
    QList<DisplayIdentitySource> sources;
    int self = -1;
    for (int i = 0; i < screens.size(); ++i) {
        if (screens[i] == screen) self = i;
        sources.append(identitySourceFor(screens[i]));
    }
    if (self < 0) { sources.append(identitySourceFor(screen)); self = sources.size() - 1; }
    return assignDisplayIds(sources).value(self);
}

QString DisplayManager::colorSpaceName(const QScreen* screen) {
    // Qt 6.8 public QScreen exposes no display color space, and reading the monitor ICC profile
    // needs a platform adapter that is not implemented (COLOR-01). Report honestly instead of "sRGB".
    Q_UNUSED(screen);
    return QStringLiteral("Unknown");
}
bool DisplayManager::isPresentationActive() const {
    return d->active;
}

PresentationMode DisplayManager::presentationMode() const {
    return d->mode;
}

QString DisplayManager::presentationModeString() const {
    switch (d->mode) {
    case PresentationMode::Pinned: return QStringLiteral("pinned");
    case PresentationMode::Comparison: return QStringLiteral("comparison");
    case PresentationMode::Mirror:
    default:
        return QStringLiteral("mirror");
    }
}

QString DisplayManager::activeDisplayId() const {
    return d->activeId;
}

QString DisplayManager::activeDisplayName() const {
    return d->activeName;
}

QString DisplayManager::currentPath() const {
    return d->currentPath;
}

QString DisplayManager::pinnedPath() const {
    return d->pinnedPath;
}

bool DisplayManager::isPinned() const {
    return d->mode == PresentationMode::Pinned;
}

quint64 DisplayManager::currentGeneration() const {
    return d->currentGeneration;
}
quint64 DisplayManager::outputGeneration() const{return d->mode==PresentationMode::Pinned?d->pinnedGeneration:d->displayedGeneration;}
QString DisplayManager::routingStatus() const{return d->routeStatus;}

bool DisplayManager::openPresentation(const QString& displayId, PresentationMode mode) {
    if(!d->active){d->folderHeld=false;emit currentImageRequested();d->followFolder=d->currentPath.isEmpty()?QString{}:QFileInfo(d->currentPath).absolutePath();}
    if (d->presentationWindow && !d->activeId.isEmpty()) {
        if (d->viewports.size()>=64 && !d->viewports.contains(d->activeId)) d->viewports.erase(d->viewports.begin());
        d->viewports[d->activeId]=d->presentationWindow->viewportState();
    }
    QScreen* targetScreen = displayId.isEmpty()?d->findScreenById(d->preferredId):nullptr;
    if (!displayId.isEmpty()) {
        targetScreen = d->findScreenById(displayId);
    }
    if (!targetScreen) {
        const auto preferred = preferredPresentationDisplay();
        if (!preferred.id.isEmpty()) {
            targetScreen = d->findScreenById(preferred.id);
        }
    }
    if (!targetScreen && !QGuiApplication::screens().isEmpty()) {
        targetScreen = QGuiApplication::screens().first();
    }
    if (!targetScreen) return false;

    d->presentationScreen = targetScreen;
    d->activeId = computeDisplayId(targetScreen);
    d->activeName = targetScreen->name();
    d->mode = mode;

    if (!d->presentationWindow) {
        if (s_presentationWindowFactory) {
            d->presentationWindow = s_presentationWindowFactory(this);
        }
    }

    if (d->presentationWindow) {
        // The live window may still describe the previous display, or have no image yet.
        d->presentationWindow->setFitMode(d->fitModes.value(d->activeId,QStringLiteral("shrink")));
        d->presentationWindow->setScreen(targetScreen);
        d->presentationWindow->setGeometry(targetScreen->geometry());
        d->applyImagesToWindow();
        const auto defaultFit=d->fitModes.value(d->activeId,QStringLiteral("shrink"));
        const QVariantMap defaults{{"primary",QVariantMap{{"fit",defaultFit}}},{"secondary",QVariantMap{{"fit",defaultFit}}},{"linked",true},{"activeSecondary",false}};
        d->presentationWindow->restoreViewport(d->viewports.value(d->activeId,defaults));
        d->presentationWindow->show();
    }

    d->active = true;
    d->status(mode==PresentationMode::Pinned?QStringLiteral("Reference pinned"):QStringLiteral("Following controller"));
    emit activeDisplayIdChanged(d->activeId);
    emit presentationModeChanged(d->mode);
    emit presentationActiveChanged(true);
    emit outputColorStatusChanged();
    return true;
}

void DisplayManager::closePresentation() {
    d->cadence.stop();
    if(d->pendingValid){d->currentImage=d->pendingImage;d->currentPath=d->pendingPath;d->displayedGeneration=d->pendingGeneration;}
    d->pendingValid=false;d->pendingImage={};d->pendingPath.clear();d->folderHeld=false;d->followFolder.clear();
    if (!d->active && !d->presentationWindow) return;
    if (d->presentationWindow && !d->activeId.isEmpty()) {
        if (d->viewports.size()>=64 && !d->viewports.contains(d->activeId)) d->viewports.erase(d->viewports.begin());
        d->viewports[d->activeId]=d->presentationWindow->viewportState();
    }

    d->active = false;
    d->activeId.clear();
    d->activeName.clear();
    d->presentationScreen = nullptr;
    // Closing releases the window (and its canvases/textures) and the non-essential pixel copies.
    d->previousImage = QImage(); d->previousPath.clear();
    d->comparisonImage = QImage(); d->comparisonPath.clear(); d->comparisonGeneration = 0;

    if (d->presentationWindow) {
        d->presentationWindow->setPrimaryImage(QImage());
        d->presentationWindow->setSecondaryImage(QImage());
        d->presentationWindow->hide();
        d->presentationWindow->deleteLater();
        d->presentationWindow.reset();
    }

    emit activeDisplayIdChanged(d->activeId);
    emit presentationActiveChanged(false);
    emit presentationClosed();
    emit outputColorStatusChanged();
    d->status("Output disabled");emit currentImageRequested();
}

void DisplayManager::togglePresentation() {
    if (isPresentationActive()) {
        closePresentation();
    } else {
        openPresentation();
    }
}

void DisplayManager::setPresentationMode(const QString& mode) {
    if (mode == QStringLiteral("pinned")) {
        setMode(PresentationMode::Pinned);
    } else if (mode == QStringLiteral("comparison")) {
        setMode(PresentationMode::Comparison);
    } else {
        setMode(PresentationMode::Mirror);
    }
}

void DisplayManager::setMode(PresentationMode mode) {
    if (d->mode == mode) return;
    d->mode = mode;
    d->folderHeld=false;d->followFolder=d->currentPath.isEmpty()?QString{}:QFileInfo(d->currentPath).absolutePath();
    d->applyImagesToWindow();
    emit presentationModeChanged(d->mode);
}

void DisplayManager::updateImage(const QString& path, const QImage& image, quint64 generation) {
    if (generation < d->currentGeneration) return;

    if(d->active&&d->mode!=PresentationMode::Pinned&&!d->publishingTimed) {
        const auto folder=QFileInfo(path).absolutePath();
        if(d->followFolder.isEmpty())d->followFolder=folder;
        if(folder!=d->followFolder) {
            const auto action=d->routingPolicy.value("folderChange","follow").toString();
            if(action=="disable"){closePresentation();}
            else if(action=="hold")d->folderHeld=true;
            else d->followFolder=folder;
        }
        if(d->active&&(!d->routingPolicy.value("followSelection",true).toBool()||d->folderHeld)) {
            d->currentGeneration=generation;d->pendingGeneration=generation;d->pendingImage=image;d->pendingPath=path;d->pendingValid=true;
            d->status(d->folderHeld?QStringLiteral("Output held after folder change; choose Follow to resume."):QStringLiteral("Output held; enable Follow controller selection to resume."));d->enforceBudget();return;
        }
        if(d->active&&d->routingPolicy.value("slideshowTiming")=="independent") {
            d->currentGeneration=generation;d->pendingGeneration=generation;d->pendingPath=path;d->pendingImage=image;d->pendingValid=true;
            d->status("Latest image pending independent presentation timing; viewport linking waits for matching images.");d->enforceBudget();if(d->pendingValid&&!d->cadence.isActive())d->cadence.start();return;
        }
    }

    if (!d->active) { d->previousImage={}; d->previousPath.clear(); }
    else if (path != d->currentPath && !d->currentImage.isNull()) {
        d->previousPath = d->currentPath;
        d->previousImage = d->currentImage;
    }
    d->currentPath = path;
    d->currentImage = image;
    d->currentGeneration = generation;
    d->displayedGeneration=generation;
    d->comparisonGenerationHighWater = (std::max)(d->comparisonGenerationHighWater, generation);
    d->enforceBudget();

    if (d->mode == PresentationMode::Mirror || d->mode == PresentationMode::Comparison) {
        d->applyImagesToWindow();
    }
    emit imageRouted(path, generation);
    d->status(!d->active?QStringLiteral("Output disabled"):d->mode==PresentationMode::Pinned?QStringLiteral("Reference pinned"):d->routingPolicy.value("slideshowTiming")=="independent"?QStringLiteral("Timed output: latest image presented"):QStringLiteral("Following controller"));
}

void DisplayManager::setRoutingPolicy(const QVariantMap& policy) {
    d->preferredId=policy.value("displayId").toString();
    const QVariantMap next{{"followSelection",policy.value("followSelection",true)},
        {"folderChange",policy.value("folderChange","follow")},{"slideshowTiming",policy.value("slideshowTiming","shared")}};
    if(next==d->routingPolicy)return;
    const auto baseline=d->pendingValid?d->pendingPath:d->currentPath;
    d->routingPolicy=next;d->folderHeld=false;d->followFolder=baseline.isEmpty()?QString{}:QFileInfo(baseline).absolutePath();
    d->cadence.stop();
    if(d->active&&next.value("followSelection").toBool()) {
        if(d->pendingValid) {
            if(next.value("slideshowTiming")=="independent")d->cadence.start();
            else {const auto image=d->pendingImage;const auto path=d->pendingPath;const auto generation=d->pendingGeneration;d->pendingValid=false;d->pendingImage={};d->pendingPath.clear();d->publishingTimed=true;updateImage(path,image,generation);d->publishingTimed=false;}
        }else emit currentImageRequested();
    }
}

void DisplayManager::synchronizePresentationScreen(QScreen* screen) {
    if(!d->active||!screen||!QGuiApplication::screens().contains(screen))return;
    const auto id=computeDisplayId(screen);if(id==d->activeId)return;
    if(d->presentationWindow&&!d->activeId.isEmpty()) {
        if(d->viewports.size()>=64&&!d->viewports.contains(d->activeId))d->viewports.erase(d->viewports.begin());
        d->viewports[d->activeId]=d->presentationWindow->viewportState();
    }
    d->presentationScreen=screen;d->activeId=id;d->activeName=screen->name();
    emit activeDisplayIdChanged(id);emit outputColorStatusChanged();
}

void DisplayManager::pinCurrentImage() {
    pinImage(d->currentPath, d->currentImage, d->displayedGeneration);
}

void DisplayManager::pinImage(const QString& path, const QImage& image, quint64 generation) {
    if (generation < d->pinnedGeneration) return;   // stale pin request
    d->pinnedPath = path;
    d->pinnedImage = image;
    d->pinnedGeneration = generation;
    {
        QScopedValueRollback<bool> keepReference(d->referenceProtected, true);
        d->enforceBudget();
    }
    if (!image.isNull() && d->pinnedImage.isNull()) return;   // shed by budget
    if (d->mode == PresentationMode::Pinned) d->applyImagesToWindow();
    setMode(PresentationMode::Pinned);
    emit pinnedPathChanged(path);
}

void DisplayManager::unpin() {
    d->pinnedPath.clear();
    d->pinnedImage = QImage();
    d->pinnedGeneration = 0;
    setMode(PresentationMode::Mirror);
    emit pinnedPathChanged(d->pinnedPath);
}

void DisplayManager::setComparisonImage(const QString& path, const QImage& image, quint64 generation) {
    if (generation < d->comparisonGenerationHighWater) return;   // stale comparison request
    d->comparisonGenerationHighWater = (std::max)(d->comparisonGenerationHighWater, generation);
    d->comparisonPath = path;
    d->comparisonImage = image;
    d->comparisonGeneration = generation;
    d->enforceBudget();
    d->applyImagesToWindow();
    setMode(PresentationMode::Comparison);
}

void DisplayManager::compareWithPinnedOrPrevious() {
    d->comparisonImage = QImage(); d->comparisonPath.clear(); d->comparisonGeneration = 0;
    d->comparisonGenerationHighWater = (std::max)(d->comparisonGenerationHighWater, d->currentGeneration);
    setMode(PresentationMode::Comparison);
    d->applyImagesToWindow();
}

bool DisplayManager::setReferenceImage(const QString& path, const QImage& image, quint64 generation) {
    if (image.isNull() || generation < d->referenceGenerationHighWater) return false;   // nothing to pin, or stale
    if (image.cacheKey() == d->referenceImage.cacheKey() && path == d->referencePath) {
        d->referenceGenerationHighWater = generation;
        return true;
    }
    // Admission: the current and pinned-output frames are kept; only optional history may make room. The previous
    // reference is being replaced, so it does not count against the new one.
    QSet<qint64> keys; qint64 essential = 0;
    for (const QImage* im : std::initializer_list<const QImage*>{&d->currentImage, &d->pinnedImage, &image}) {
        if (im->isNull() || keys.contains(im->cacheKey())) continue;
        keys.insert(im->cacheKey()); essential += im->sizeInBytes();
    }
    if (essential > s_imageBudgetBytes) return false;
    d->referenceGenerationHighWater = generation;
    d->referencePath = path;
    d->referenceImage = image;
    {
        QScopedValueRollback<bool> keepReference(d->referenceProtected, true);
        d->enforceBudget();
    }
    emit referenceChanged();
    return true;
}

void DisplayManager::clearReferenceImage() {
    if (d->referenceImage.isNull() && d->referencePath.isEmpty()) return;
    d->referenceImage = QImage(); d->referencePath.clear();
    emit referenceChanged();
}

QImage DisplayManager::referenceImage() const { return d->referenceImage; }
QString DisplayManager::referencePath() const { return d->referencePath; }

static bool isValidFitMode(const QString& mode) {
    return (mode == QLatin1String("shrink") || mode == QLatin1String("fit") ||
            mode == QLatin1String("fill") || mode == QLatin1String("width") ||
            mode == QLatin1String("height") || mode==QLatin1String("actual"));
}

void DisplayManager::setOutputFitMode(const QString& mode) {
    if (!isValidFitMode(mode)) return;
    if (!d->active || d->activeId.isEmpty()) return;
    if (d->fitModes.size()>=64 && !d->fitModes.contains(d->activeId)) d->fitModes.erase(d->fitModes.begin());
    d->fitModes[d->activeId] = mode;
    if (d->presentationWindow) d->presentationWindow->setFitMode(mode);
}
void DisplayManager::outputViewportCommand(const QString& command) {
    if (!d->active || !d->presentationWindow) return;
    d->presentationWindow->viewportCommand(command);
}
QVariantMap DisplayManager::outputViewportState(const QString& displayId) const {
    if (d->presentationWindow && (displayId.isEmpty() || displayId==d->activeId)) return d->presentationWindow->viewportState();
    return d->viewports.value(displayId);
}
void DisplayManager::setOutputBindingProvider(std::function<QString(const QString&)> provider) {
    d->outputBindingProvider=std::move(provider);
}
QString DisplayManager::outputBinding(const QString& command) const {
    return d->outputBindingProvider ? d->outputBindingProvider(command) : QString();
}

QString DisplayManager::outputFitMode(const QString& displayId) const {
    if (d->presentationWindow && (displayId.isEmpty() || displayId==d->activeId)) {
        const auto fit=d->presentationWindow->viewportState().value("primary").toMap().value("fit").toString();
        return fit.isEmpty()?QStringLiteral("custom"):fit;
    }
    return d->fitModes.value(displayId.isEmpty() ? d->activeId : displayId, QStringLiteral("shrink"));
}
QString DisplayManager::outputColorStatus() const {
    return d->presentationWindow?d->presentationWindow->outputColorStatus():QStringLiteral("Presentation closed");
}
qint64 DisplayManager::outputColorResourceBytes() const {
    return d->presentationWindow?d->presentationWindow->colorResourceBytes():0;
}

qint64 DisplayManager::retainedImageBytes() const { return d->retainedBytes(); }

void DisplayManager::releaseCachedImages() {
    d->pendingImage=QImage();d->pendingPath.clear();d->pendingValid=false;d->cadence.stop();
    d->previousImage = QImage(); d->previousPath.clear();
    d->comparisonImage = QImage(); d->comparisonPath.clear(); d->comparisonGeneration = 0;
    d->applyImagesToWindow();
    if (d->presentationWindow && d->mode != PresentationMode::Comparison) {
        d->presentationWindow->setSecondaryImage(QImage());
    }
}

void DisplayManager::setToggleBindingProvider(std::function<QString()> provider) {
    d->toggleBindingProvider = std::move(provider);
}

QString DisplayManager::toggleBinding() const {
    return d->toggleBindingProvider ? d->toggleBindingProvider() : QStringLiteral("F8");
}

QQuickWindow* DisplayManager::presentationWindow() const {
    return d->presentationWindow ? d->presentationWindow->quickWindow() : nullptr;
}

} // namespace glancelot
