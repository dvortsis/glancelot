#include "glancelot/directory_entry_model.hpp"

// Contract stub (lead freeze). FILE-GRID-01 implementation replaces these bodies.
struct DirectoryEntryModel::Private {
    QString directory, currentImage;
    quint64 epoch{1};
};
DirectoryEntryModel::DirectoryEntryModel(QObject* parent) : QAbstractListModel(parent), d(std::make_unique<Private>()) {}
DirectoryEntryModel::~DirectoryEntryModel() = default;
int DirectoryEntryModel::rowCount(const QModelIndex&) const { return 0; }
QVariant DirectoryEntryModel::data(const QModelIndex&, int) const { return {}; }
QHash<int, QByteArray> DirectoryEntryModel::roleNames() const {
    return {{NameRole, "name"}, {PathRole, "path"}, {KindRole, "kind"}, {LinkRole, "isLink"}, {HiddenRole, "isHidden"},
            {SizeRole, "size"}, {ModifiedRole, "modified"}, {SelectedRole, "isSelected"}, {CursorRole, "isCursor"},
            {CurrentRole, "isCurrent"}};
}
QString DirectoryEntryModel::directory() const { return d->directory; }
int DirectoryEntryModel::count() const { return 0; }
bool DirectoryEntryModel::loading() const { return false; }
bool DirectoryEntryModel::accessible() const { return true; }
bool DirectoryEntryModel::truncated() const { return false; }
void DirectoryEntryModel::setDirectory(const QString&) {}
void DirectoryEntryModel::refresh() {}
void DirectoryEntryModel::setActive(bool) {}
void DirectoryEntryModel::setSort(const QString&, bool) {}
void DirectoryEntryModel::setIncludeHidden(bool) {}
void DirectoryEntryModel::select(int, SelectMode) {}
void DirectoryEntryModel::selectAll() {}
void DirectoryEntryModel::clearSelection() {}
int DirectoryEntryModel::selectedCount() const { return 0; }
int DirectoryEntryModel::cursorRow() const { return -1; }
QStringList DirectoryEntryModel::selectedPaths() const { return {}; }
int DirectoryEntryModel::rowOf(const QString&) const { return -1; }
QString DirectoryEntryModel::pathAt(int) const { return {}; }
QString DirectoryEntryModel::kindAt(int) const { return {}; }
QString DirectoryEntryModel::currentImage() const { return d->currentImage; }
void DirectoryEntryModel::setCurrentImage(const QString& path) { if (path != d->currentImage) { d->currentImage = path; emit currentImageChanged(); } }
quint64 DirectoryEntryModel::epoch() const { return d->epoch; }
void DirectoryEntryModel::invalidate() { ++d->epoch; emit epochChanged(); }
QString DirectoryEntryModel::thumbnailSource(int, int) const { return {}; }
