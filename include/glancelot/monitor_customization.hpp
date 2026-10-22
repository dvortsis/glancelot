#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class PaneLayout;
class QQuickWindow;

namespace glancelot {
class DisplayManager;

// Lead-frozen MONITOR-CUSTOM-01 contract. See docs/MONITOR-CUSTOM-01-PLAN.md
// for validation, persistence, command parity, ownership and recovery semantics.
// Implementation belongs to the user-reassigned Codex packet.
class MonitorCustomization final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap outputOptions READ outputOptions NOTIFY changed)
    Q_PROPERTY(QVariantMap paneRoutes READ paneRoutes NOTIFY changed)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY changed)
    Q_PROPERTY(bool layoutLocked READ layoutLocked WRITE setLayoutLocked NOTIFY changed)
    Q_PROPERTY(QString feedback READ feedback NOTIFY changed)
    Q_PROPERTY(QString outputRoutingStatus READ outputRoutingStatus NOTIFY changed)
public:
    explicit MonitorCustomization(DisplayManager* displays, PaneLayout* panes, QObject* parent = nullptr);
    ~MonitorCustomization() override;
    void setSlot(int slot);
    Q_INVOKABLE void reloadPreferences();
    void setControllerWindow(QQuickWindow* window);
    QVariantMap outputOptions() const;
    QVariantMap paneRoutes() const;
    QVariantList presets() const;
    bool layoutLocked() const;
    QString feedback() const;
    QString outputRoutingStatus() const;
    void setLayoutLocked(bool locked);
    Q_INVOKABLE bool setOutputOptions(const QVariantMap& patch);
    Q_INVOKABLE bool syncControllerViewport(const QVariantMap& state, quint64 generation);
    Q_INVOKABLE bool identifyDisplays();
    Q_INVOKABLE bool moveOutput(const QString& displayId);
    Q_INVOKABLE bool swapControllerAndOutput();
    Q_INVOKABLE bool detachPane(const QString& paneId, const QString& displayId);
    Q_INVOKABLE bool redockPane(const QString& paneId);
    Q_INVOKABLE bool setPaneFrozen(const QString& paneId, bool frozen);
    Q_INVOKABLE bool updatePaneGeometry(const QString& paneId, const QVariantMap& geometry);
    Q_INVOKABLE bool savePreset(const QString& id, const QString& name, const QString& kind);
    Q_INVOKABLE bool renamePreset(const QString& id, const QString& name);
    Q_INVOKABLE bool deletePreset(const QString& id);
    Q_INVOKABLE bool applyPreset(const QString& id);
    Q_INVOKABLE bool resetPreferences();
    Q_INVOKABLE void recoverToController();
signals:
    void changed();
    void controllerViewportRequested(const QVariantMap& state, quint64 generation);
    void commandRequested(const QString& command);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
} // namespace glancelot
