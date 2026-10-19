#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QJsonObject>

// Lead-frozen PANES-01 state. Display constraints must never overwrite preferred geometry.
class PaneLayout final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap panes READ panes NOTIFY changed)
    Q_PROPERTY(QVariantMap centerSplit READ centerSplit NOTIFY changed)
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY changed)
    Q_PROPERTY(QString feedback READ feedback NOTIFY changed)
    Q_PROPERTY(QString contentMode READ contentMode NOTIFY changed)   // "preview" | "grid" | "combined"
    // F049 named layout presets: three built-ins plus up to 12 user presets stored once for all windows ("layoutPresets").
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
public:
    explicit PaneLayout(QObject* parent=nullptr);
    QVariantMap panes() const;
    QVariantMap centerSplit() const { return center_.toVariantMap(); }
    Q_INVOKABLE bool setCenterOrientation(const QString& orientation);
    Q_INVOKABLE bool setCenterRatio(double ratio);
    QString contentMode() const;
    Q_INVOKABLE bool setContentMode(const QString& mode);              // false for unknown values; no-op when equal
    bool locked() const { return locked_; }
    QString feedback() const { return feedback_; }
    void setSlot(int slot);
    void setLocked(bool locked);
    Q_INVOKABLE bool move(const QString& id,const QString& region,int order);
    Q_INVOKABLE bool resize(const QString& id,double extent);
    Q_INVOKABLE bool freeze(const QString& id,bool frozen);
    Q_INVOKABLE bool setVisible(const QString& id,bool visible);
    Q_INVOKABLE void reset();
    Q_INVOKABLE QVariantMap pane(const QString& id) const;
    QStringList presetNames() const;                                  // built-ins first, then user presets by name
    Q_INVOKABLE bool isBuiltInPreset(const QString& name) const;
    Q_INVOKABLE bool savePreset(const QString& name);                 // current geometry, visibility and content mode; not lock or freeze
    Q_INVOKABLE bool applyPreset(const QString& name);                // refused while locked or while any pane is frozen
    Q_INVOKABLE bool deletePreset(const QString& name);               // user presets only
signals:
    void changed();
    void presetsChanged();
private:
    int slot_{};
    bool locked_{};
    QString feedback_;
    QJsonObject panes_;
    QJsonObject center_{{"orientation","horizontal"},{"ratio",0.5}};
    QString mode_{QStringLiteral("preview")};
    bool editCenter(const QJsonObject& fields);
    QJsonObject userPresets() const;
    QJsonObject presetObject(const QString& name) const;
    void persistWhole();
    QStringList builtIns() const;
    static QJsonObject defaults();
    void load();
    bool edit(const QString& id,const QJsonObject& fields,bool geometry);
    void persist(const QString& id,const QJsonObject& fields,bool reset=false,bool lockOnly=false);
};
