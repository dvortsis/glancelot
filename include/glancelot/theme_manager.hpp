#pragma once
#include <QObject>
#include <QVariantMap>
#include <QVariantList>

class ThemeManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList families READ families CONSTANT)
    Q_PROPERTY(QString family READ family WRITE setFamily NOTIFY changed)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit ThemeManager(QObject* parent=nullptr);
    QVariantList families() const { return m_families; }
    QString family() const { return m_family; }
    QString mode() const { return m_mode; }
    bool dark() const;
    QVariantMap colors() const;
    QString error() const { return m_error; }
    void setFamily(const QString& family);
    void setMode(const QString& mode);
    Q_INVOKABLE QVariantMap preview(const QString& family, bool dark) const;
signals:
    void changed();
private:
    void save(const char* key);
    QVariantList m_families;
    QString m_family{"slate-ice"},m_mode{"system"},m_error;
};
