#include "glancelot/favorites_model.hpp"
#include "glancelot/settings_file.hpp"
#include "settings_retry.hpp"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <vector>

namespace {
QString normalizePath(const QString& path) {
    if (path.isEmpty()) return QString();
    QString clean = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    clean = QDir::toNativeSeparators(clean);
#endif
    return clean;
}

bool pathsEqual(const QString& a, const QString& b) {
#ifdef Q_OS_WIN
    return QString::compare(normalizePath(a), normalizePath(b), Qt::CaseInsensitive) == 0;
#else
    return normalizePath(a) == normalizePath(b);
#endif
}

QString displayNameForPath(const QString& path) {
    const QString clean = normalizePath(path);
    if (clean.isEmpty()) return QString();
    const QFileInfo fi(clean);
    const QString base = fi.fileName();
    if (!base.isEmpty()) return base;
    return clean; // Drive root or filesystem root
}
}

struct FavoritesModel::Private {
    struct Item {
        QString path;
        QString alias;
    };
    std::vector<Item> items;
    QString currentDirectory;

    int indexOfPath(const QString& path) const {
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (pathsEqual(items[i].path, path)) return int(i);
        }
        return -1;
    }

    void load() {
        items.clear();
        QJsonObject settings;
        if (!glancelot::readSettings(settings).ok()) return;
        const auto favValue = settings.value("favorites");
        if (!favValue.isArray()) return;
        const auto favArray = favValue.toArray();
        for (const auto& val : favArray) {
            if (val.isObject()) {
                const auto obj = val.toObject();
                const QString path = normalizePath(obj.value("path").toString());
                const QString alias = obj.value("alias").toString().trimmed();
                if (!path.isEmpty() && indexOfPath(path) < 0) {
                    items.push_back({path, alias});
                }
            } else if (val.isString()) {
                const QString path = normalizePath(val.toString());
                if (!path.isEmpty() && indexOfPath(path) < 0) {
                    items.push_back({path, QString()});
                }
            }
        }
    }

    bool save(QObject* owner) {
        QJsonArray favArray;
        for (const auto& item : items) {
            QJsonObject obj;
            obj["path"] = item.path;
            if (!item.alias.isEmpty()) obj["alias"] = item.alias;
            favArray.append(obj);
        }
        return glancelot::settings_detail::saveWithRetry(owner,"favorites",[favArray](QJsonObject& settings) {
            settings["favorites"] = favArray;
        });
    }
};

FavoritesModel::FavoritesModel(QObject* parent)
    : QAbstractListModel(parent), d(std::make_unique<Private>()) {
    d->load();
}

FavoritesModel::~FavoritesModel() = default;

int FavoritesModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return int(d->items.size());
}

int FavoritesModel::count() const {
    return int(d->items.size());
}

QString FavoritesModel::currentDirectory() const {
    return d->currentDirectory;
}

void FavoritesModel::setCurrentDirectory(const QString& dir) {
    const QString normalized = normalizePath(dir);
    if (pathsEqual(d->currentDirectory, normalized)) return;
    d->currentDirectory = normalized;
    emit currentDirectoryChanged();
    if (!d->items.empty()) {
        emit dataChanged(index(0), index(int(d->items.size()) - 1), {IsCurrentRole});
    }
}

QVariant FavoritesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= int(d->items.size())) return {};
    const auto& item = d->items[std::size_t(index.row())];
    switch (role) {
    case PathRole:
        return item.path;
    case NameRole:
        return item.alias.isEmpty() ? displayNameForPath(item.path) : item.alias;
    case AliasRole:
        return item.alias;
    case IsCurrentRole:
        return pathsEqual(item.path, d->currentDirectory);
    case IndexRole:
        return index.row();
    default:
        return {};
    }
}

QHash<int, QByteArray> FavoritesModel::roleNames() const {
    return {
        {PathRole, "path"},
        {NameRole, "name"},
        {AliasRole, "alias"},
        {IsCurrentRole, "isCurrent"},
        {IndexRole, "itemIndex"}
    };
}

bool FavoritesModel::addFavorite(const QString& path, const QString& alias) {
    const QString clean = normalizePath(path);
    if (clean.isEmpty()) return false;
    const int existing = d->indexOfPath(clean);
    if (existing >= 0) {
        if (!alias.isEmpty() && d->items[std::size_t(existing)].alias != alias) {
            d->items[std::size_t(existing)].alias = alias.trimmed();
            d->save(this);
            emit dataChanged(index(existing), index(existing), {NameRole, AliasRole});
        }
        return true;
    }
    const int insertIdx = int(d->items.size());
    beginInsertRows(QModelIndex(), insertIdx, insertIdx);
    d->items.push_back({clean, alias.trimmed()});
    endInsertRows();
    d->save(this);
    emit countChanged();
    return true;
}

bool FavoritesModel::removeFavorite(const QString& path) {
    const int idx = d->indexOfPath(path);
    if (idx < 0) return false;
    return removeAt(idx);
}

bool FavoritesModel::removeAt(int index) {
    if (index < 0 || index >= int(d->items.size())) return false;
    beginRemoveRows(QModelIndex(), index, index);
    d->items.erase(d->items.begin() + index);
    endRemoveRows();
    d->save(this);
    emit countChanged();
    return true;
}

bool FavoritesModel::moveUp(int index) {
    if (index <= 0 || index >= int(d->items.size())) return false;
    return move(index, index - 1);
}

bool FavoritesModel::moveDown(int index) {
    if (index < 0 || index + 1 >= int(d->items.size())) return false;
    return move(index, index + 1);
}

bool FavoritesModel::move(int from, int to) {
    if (from < 0 || from >= int(d->items.size()) || to < 0 || to >= int(d->items.size()) || from == to) {
        return false;
    }
    const int destForQt = to > from ? to + 1 : to;
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), destForQt)) return false;
    auto item = std::move(d->items[std::size_t(from)]);
    d->items.erase(d->items.begin() + from);
    d->items.insert(d->items.begin() + to, std::move(item));
    endMoveRows();
    d->save(this);
    return true;
}

bool FavoritesModel::setAlias(const QString& path, const QString& alias) {
    const int idx = d->indexOfPath(path);
    if (idx < 0) return false;
    d->items[std::size_t(idx)].alias = alias.trimmed();
    d->save(this);
    emit dataChanged(index(idx), index(idx), {NameRole, AliasRole});
    return true;
}

bool FavoritesModel::isFavorite(const QString& path) const {
    return d->indexOfPath(path) >= 0;
}

QString FavoritesModel::aliasForPath(const QString& path) const {
    const int idx = d->indexOfPath(path);
    if (idx < 0) return QString();
    return d->items[std::size_t(idx)].alias;
}

QVariantMap FavoritesModel::get(int index) const {
    if (index < 0 || index >= int(d->items.size())) return {};
    const auto& item = d->items[std::size_t(index)];
    QVariantMap map;
    map["path"] = item.path;
    map["name"] = item.alias.isEmpty() ? displayNameForPath(item.path) : item.alias;
    map["alias"] = item.alias;
    map["isCurrent"] = pathsEqual(item.path, d->currentDirectory);
    map["itemIndex"] = index;
    return map;
}

void FavoritesModel::reload() {
    beginResetModel();
    d->load();
    endResetModel();
    emit countChanged();
}

