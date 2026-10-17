#pragma once
#include "contracts.hpp"
#include <QAbstractListModel>
#include <QStringList>

// Read-only list of the current folder's eligible images for the virtualized filmstrip.
// sync() is called on every navigation state change; it only resets the model when the folder
// listing changed, so ListView delegates (and their in-flight thumbnails) survive ordinary stepping.
class FilmstripModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentRowChanged)
    Q_PROPERTY(quint64 epoch READ epoch NOTIFY epochChanged)
public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, CurrentRole };
    explicit FilmstripModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
    int currentRow() const;
    quint64 epoch() const;
    void sync(const glancelot::NavigationSnapshot& snapshot);
    // Content on disk may have changed; delegates must request fresh thumbnails.
    void invalidate();
    Q_INVOKABLE QString pathAt(int row) const;
    // image:// URL for a delegate. Passing the epoch makes QML bindings re-request after invalidate().
    Q_INVOKABLE QString thumbnailSource(const QString& path, int edge, quint64 epoch) const;
signals:
    void countChanged();
    void currentRowChanged();
    void epochChanged();
private:
    QString directory_;
    QStringList paths_;
    int current_{-1};
    quint64 epoch_{1};
};
