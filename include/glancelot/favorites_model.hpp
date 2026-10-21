#pragma once
#include <QAbstractListModel>
#include <memory>

// Persistent folder favorites model for the left HUD (FAVORITES-01 / F003 / UX-03 / SYS-03).
// Contract (frozen by the lead; change only through the lead):
//  * Stored in settings.json under the "favorites" key as an array of objects ({path, alias}) or strings.
//  * Zero disk-waking / blocking probes at startup or listing: missing or disconnected mounts must not stall
//    the GUI or wake sleeping disks. Activating a favorite delegates to ViewerController.
//  * Mutating operations (add, remove, reorder, alias) perform atomic settings updates via glancelot::updateSettings,
//    preserving all other settings sections, unknown keys, and window slots.
//  * Supports full keyboard reordering (moveUp, moveDown, move) and selection tracking.
class FavoritesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString currentDirectory READ currentDirectory WRITE setCurrentDirectory NOTIFY currentDirectoryChanged)
public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        NameRole,
        AliasRole,
        IsCurrentRole,
        IndexRole
    };
    Q_ENUM(Role)

    explicit FavoritesModel(QObject* parent = nullptr);
    ~FavoritesModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    QString currentDirectory() const;
    void setCurrentDirectory(const QString& dir);

    Q_INVOKABLE bool addFavorite(const QString& path, const QString& alias = QString());
    Q_INVOKABLE bool removeFavorite(const QString& path);
    Q_INVOKABLE bool removeAt(int index);
    Q_INVOKABLE bool moveUp(int index);
    Q_INVOKABLE bool moveDown(int index);
    Q_INVOKABLE bool move(int from, int to);
    Q_INVOKABLE bool setAlias(const QString& path, const QString& alias);
    Q_INVOKABLE bool isFavorite(const QString& path) const;
    Q_INVOKABLE QString aliasForPath(const QString& path) const;
    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE void reload();

signals:
    void countChanged();
    void currentDirectoryChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};
