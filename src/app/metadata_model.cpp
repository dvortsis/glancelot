#include "glancelot/metadata_model.hpp"
#include <QGuiApplication>
#include <QClipboard>

struct MetadataModel::Private {
    glancelot::MetadataRecord record;
    QList<glancelot::MetadataItem> filteredItems;
    bool loading = false;
    QString searchFilter;
    QString categoryFilter = QStringLiteral("All");

    void applyFilter() {
        filteredItems.clear();
        const QString search = searchFilter.trimmed();
        const bool matchCat = !categoryFilter.isEmpty() && categoryFilter != QStringLiteral("All");

        for (const auto& item : record.items) {
            if (matchCat && !item.category.contains(categoryFilter, Qt::CaseInsensitive)) {
                continue;
            }
            if (!search.isEmpty()) {
                const bool hit = item.label.contains(search, Qt::CaseInsensitive) ||
                                 item.value.contains(search, Qt::CaseInsensitive) ||
                                 item.key.contains(search, Qt::CaseInsensitive) ||
                                 item.rawValue.contains(search, Qt::CaseInsensitive);
                if (!hit) continue;
            }
            filteredItems.append(item);
        }
    }
};

MetadataModel::MetadataModel(QObject* parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<Private>())
{
}

MetadataModel::~MetadataModel() = default;

int MetadataModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(d->filteredItems.size());
}

QVariant MetadataModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= d->filteredItems.size()) {
        return {};
    }
    const auto& item = d->filteredItems.at(index.row());
    switch (role) {
    case KeyRole: return item.key;
    case LabelRole: return item.label;
    case ValueRole: return item.value;
    case RawValueRole: return item.rawValue;
    case CategoryRole: return item.category;
    default: return {};
    }
}

QHash<int, QByteArray> MetadataModel::roleNames() const {
    return {
        {KeyRole, "key"},
        {LabelRole, "label"},
        {ValueRole, "value"},
        {RawValueRole, "rawValue"},
        {CategoryRole, "category"}
    };
}

int MetadataModel::count() const {
    return static_cast<int>(d->filteredItems.size());
}

bool MetadataModel::loading() const {
    return d->loading;
}

bool MetadataModel::hasMetadata() const {
    return d->record.valid && !d->record.items.isEmpty();
}

QString MetadataModel::searchFilter() const {
    return d->searchFilter;
}

void MetadataModel::setSearchFilter(const QString& filter) {
    if (d->searchFilter == filter) return;
    beginResetModel();
    d->searchFilter = filter;
    d->applyFilter();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

QString MetadataModel::categoryFilter() const {
    return d->categoryFilter;
}

void MetadataModel::setCategoryFilter(const QString& category) {
    if (d->categoryFilter == category) return;
    beginResetModel();
    d->categoryFilter = category;
    d->applyFilter();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

QString MetadataModel::cameraSummary() const {
    if (d->record.cameraMake.isEmpty() && d->record.cameraModel.isEmpty()) {
        return QStringLiteral("Unknown Camera");
    }
    if (d->record.cameraMake.isEmpty()) return d->record.cameraModel;
    if (d->record.cameraModel.isEmpty()) return d->record.cameraMake;
    if (d->record.cameraModel.startsWith(d->record.cameraMake, Qt::CaseInsensitive)) {
        return d->record.cameraModel;
    }
    return d->record.cameraMake + QStringLiteral(" ") + d->record.cameraModel;
}

QString MetadataModel::exposureSummary() const {
    QStringList parts;
    if (!d->record.exposureTime.isEmpty()) parts << d->record.exposureTime;
    if (!d->record.fNumber.isEmpty()) parts << d->record.fNumber;
    if (!d->record.isoRating.isEmpty()) parts << (QStringLiteral("ISO ") + d->record.isoRating);
    if (!d->record.focalLength.isEmpty()) parts << d->record.focalLength;
    return parts.join(QStringLiteral("  •  "));
}

QString MetadataModel::dimensionsSummary() const {
    if (d->record.displayWidth <= 0 || d->record.displayHeight <= 0) return {};
    const double mp = (d->record.displayWidth * static_cast<double>(d->record.displayHeight)) / 1000000.0;
    return QString::asprintf("%d × %d (%.1f MP)", d->record.displayWidth, d->record.displayHeight, mp);
}

QString MetadataModel::dateTimeSummary() const {
    return d->record.dateTimeOriginal.isEmpty() ? d->record.dateTimeDigitized : d->record.dateTimeOriginal;
}

QString MetadataModel::locationSummary() const {
    if (!d->record.hasGps) return {};
    if (!d->record.gpsLatitudeStr.isEmpty() && !d->record.gpsLongitudeStr.isEmpty()) {
        return d->record.gpsLatitudeStr + QStringLiteral(", ") + d->record.gpsLongitudeStr;
    }
    return QString::asprintf("%.6f, %.6f", d->record.latitude, d->record.longitude);
}

bool MetadataModel::hasGps() const {
    return d->record.hasGps;
}

QString MetadataModel::filePath() const {
    return d->record.filePath;
}

const glancelot::MetadataRecord& MetadataModel::record() const {
    return d->record;
}

void MetadataModel::setRecord(const glancelot::MetadataRecord& record) {
    beginResetModel();
    d->record = record;
    d->loading = false;
    d->applyFilter();
    endResetModel();
    emit recordChanged();
    emit countChanged();
    emit loadingChanged();
}

void MetadataModel::setLoading(bool loading) {
    if (d->loading == loading) return;
    d->loading = loading;
    emit loadingChanged();
}

void MetadataModel::clear() {
    beginResetModel();
    d->record = glancelot::MetadataRecord{};
    d->filteredItems.clear();
    d->loading = false;
    endResetModel();
    emit recordChanged();
    emit countChanged();
    emit loadingChanged();
}

QString MetadataModel::copySummary() const {
    QStringList lines;
    if (!d->record.fileName.isEmpty()) lines << (QStringLiteral("File: ") + d->record.fileName);
    const QString dims = dimensionsSummary();
    if (!dims.isEmpty()) lines << (QStringLiteral("Dimensions: ") + dims);
    const QString cam = cameraSummary();
    if (!cam.isEmpty() && cam != QStringLiteral("Unknown Camera")) lines << (QStringLiteral("Camera: ") + cam);
    if (!d->record.lensModel.isEmpty()) lines << (QStringLiteral("Lens: ") + d->record.lensModel);
    const QString exp = exposureSummary();
    if (!exp.isEmpty()) lines << (QStringLiteral("Exposure: ") + exp);
    const QString dt = dateTimeSummary();
    if (!dt.isEmpty()) lines << (QStringLiteral("Date: ") + dt);
    const QString loc = locationSummary();
    if (!loc.isEmpty()) lines << (QStringLiteral("Location: ") + loc);

    const QString text = lines.join(QStringLiteral("\n"));
    if (auto* clip = QGuiApplication::clipboard()) {
        clip->setText(text);
    }
    return text;
}

QString MetadataModel::copyGps() const {
    const QString loc = locationSummary();
    if (!loc.isEmpty()) {
        if (auto* clip = QGuiApplication::clipboard()) {
            clip->setText(loc);
        }
    }
    return loc;
}

QString MetadataModel::copyTag(const QString& key) const {
    for (const auto& item : d->record.items) {
        if (item.key == key) {
            if (auto* clip = QGuiApplication::clipboard()) {
                clip->setText(item.value);
            }
            return item.value;
        }
    }
    return {};
}

QVariantMap MetadataModel::get(int index) const {
    if (index < 0 || index >= d->filteredItems.size()) return {};
    const auto& item = d->filteredItems.at(index);
    return {
        {QStringLiteral("key"), item.key},
        {QStringLiteral("label"), item.label},
        {QStringLiteral("value"), item.value},
        {QStringLiteral("rawValue"), item.rawValue},
        {QStringLiteral("category"), item.category}
    };
}

