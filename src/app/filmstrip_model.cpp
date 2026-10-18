#include "glancelot/filmstrip_model.hpp"
#include "thumbnail_id.hpp"
#include <QDir>
#include <QFile>

namespace {
QString displayPath(const std::filesystem::path& path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
#else
    return QFile::decodeName(path.native().c_str());
#endif
}
}

QString FilmstripModel::thumbnailSource(const QString& path, int edge, quint64 epoch) const {
    return "image://thumb/" + thumbnail_id::encode(epoch, edge, path);
}

FilmstripModel::FilmstripModel(QObject* parent) : QAbstractListModel(parent) {}
int FilmstripModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(paths_.size()); }
int FilmstripModel::count() const { return int(paths_.size()); }
int FilmstripModel::currentRow() const { return current_; }
quint64 FilmstripModel::epoch() const { return epoch_; }
QString FilmstripModel::pathAt(int row) const { return row >= 0 && row < paths_.size() ? paths_.at(row) : QString{}; }

QVariant FilmstripModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= paths_.size()) return {};
    const QString& path = paths_.at(index.row());
    switch (role) {
    case NameRole: return path.mid(std::max(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\'))) + 1);
    case PathRole: return path;
    case CurrentRole: return index.row() == current_;
    default: return {};
    }
}

QHash<int, QByteArray> FilmstripModel::roleNames() const {
    return {{NameRole, "name"}, {PathRole, "path"}, {CurrentRole, "isCurrent"}};
}

void FilmstripModel::sync(const glancelot::NavigationSnapshot& snapshot) {
    const QString directory = displayPath(snapshot.directory);
    bool same = directory == directory_ && snapshot.files.size() == std::size_t(paths_.size());
    QStringList incoming;
    // Converting the listing is O(n) string work, but only when the length matches; this keeps rapid
    // stepping through a 10,000-image folder from rebuilding the model for every image.
    if (same) {
        for (std::size_t i = 0; i < snapshot.files.size() && same; ++i) same = displayPath(snapshot.files[i]) == paths_.at(int(i));
    }
    if (!same) {
        incoming.reserve(int(snapshot.files.size()));
        for (const auto& file : snapshot.files) incoming.append(displayPath(file));
    }
    int row = -1;
    if (!snapshot.current.empty()) {
        const QString current = displayPath(snapshot.current);
        const QStringList& list = same ? paths_ : incoming;
        row = int(list.indexOf(current));
    }
    if (!same) {
        // A folder may have changed while it was not watched. Never reuse its old path-only
        // thumbnail cache entries when returning from another location. Ordinary stepping
        // and sort changes within the same folder keep their cache.
        if (directory != directory_) invalidate();
        const int oldCount = int(paths_.size());
        beginResetModel();
        directory_ = directory; paths_ = std::move(incoming); current_ = row;
        endResetModel();
        if (oldCount != paths_.size()) emit countChanged();
        emit currentRowChanged();
        return;
    }
    if (row == current_) return;
    const int previous = current_;
    current_ = row;
    if (previous >= 0 && previous < paths_.size()) emit dataChanged(index(previous), index(previous), {CurrentRole});
    if (row >= 0) emit dataChanged(index(row), index(row), {CurrentRole});
    emit currentRowChanged();
}

void FilmstripModel::invalidate() {
    ++epoch_;
    emit epochChanged();
}
