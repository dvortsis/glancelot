#include "glancelot/folder_tree_model.hpp"
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QHash>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <vector>

namespace {
namespace fs = std::filesystem;
QString displayPath(const fs::path& path) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.native());
#else
    return QFile::decodeName(path.native().c_str());
#endif
}
fs::path nativePath(const QString& path) {
#ifdef Q_OS_WIN
    return fs::path(path.toStdWString());
#else
    return fs::path(QFile::encodeName(path).constData());
#endif
}
// Comparison key: forward slashes, no trailing separator (except a bare root), case-folded where the OS is.
QString keyFor(const QString& path) {
    if (path.isEmpty()) return {};
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    key = key.toLower();
#endif
    return key;
}
bool isSameOrUnder(const QString& key, const QString& ancestor) {
    if (key == ancestor) return true;
    if (!key.startsWith(ancestor)) return false;
    return ancestor.endsWith(QLatin1Char('/')) || key.at(ancestor.size()) == QLatin1Char('/');
}
}

struct FolderTreeModel::Private {
    struct Node {
        QString path, name, key;
        int depth{};
        bool expanded{}, loading{};
        NodeState state{Unknown};
        quint64 seq{};
    };
    struct Load { std::stop_source stop; quint64 seq{}; bool started{}; };

    FolderTreeModel* q;
    std::vector<Node> rows;
    QString rootKey, currentKey, revealKey, rootText;
    quint64 epoch{1}, nextSeq{1};
    int current{-1}, inFlight{};
    bool canRaise{}, computer{};
    Lister lister{[](const fs::path& path, std::stop_token cancel) { return glancelot::childFolders(path, cancel, kMaxChildren, true); }};
    // Heap-allocated so a listing that ignores cancellation (dead network share) can be abandoned at teardown
    // instead of freezing the GUI thread; the leaked pool and its thread hold only by-value captures.
    QThreadPool* pool = new QThreadPool;
    QHash<QString, Load> loads;

    // Two workers: one wedged listing (e.g. a dead network share) must not stall every other expansion.
    explicit Private(FolderTreeModel* owner) : q(owner) { pool->setMaxThreadCount(kMaxConcurrentLoads); }
    ~Private() {
        for (auto& load : loads) load.stop.request_stop();
        if (pool->waitForDone(1000)) delete pool;
        // else: abandon the pool; its worker finishes (or stays wedged in the OS) without touching this object.
    }

    int indexOfKey(const QString& key) const {
        for (int i = 0; i < int(rows.size()); ++i) if (rows[std::size_t(i)].key == key) return i;
        return -1;
    }
    void changed(int row, const QList<int>& roles) { if (row >= 0 && row < int(rows.size())) emit q->dataChanged(q->index(row), q->index(row), roles); }
    void refreshCurrent() {
        const int row = currentKey.isEmpty() ? -1 : indexOfKey(currentKey);
        if (row == current) return;
        const int previous = current;
        current = row;
        changed(previous, {IsCurrentRole});
        changed(row, {IsCurrentRole});
        emit q->currentRowChanged();
    }
    void cancelAll() {
        for (auto& load : loads) load.stop.request_stop();
        loads.clear();
    }
    void resetRoot(const fs::path& root) {
        cancelAll();
        ++epoch;
        const bool hadRows = !rows.empty();
        q->beginResetModel();
        rows.clear();
        current = -1;
        if (!root.empty()) {
            Node node;
            node.path = displayPath(root);
            node.key = keyFor(node.path);
            node.depth = 0;
            const auto leaf = root.filename();
            node.name = leaf.empty() ? node.path : displayPath(leaf);
            rows.push_back(node);
        }
        q->endResetModel();
        rootKey = rows.empty() ? QString{} : rows.front().key;
        const auto parent = root.parent_path();
        canRaise = !root.empty() && !parent.empty() && parent != root;
        if (hadRows || !rows.empty()) { emit q->countChanged(); }
        emit q->rootChanged();
        if (!rows.empty()) startLoad(0);
    }
    void startLoad(int row) {
        Node& node = rows[std::size_t(row)];
        if (loads.contains(node.key)) return;
        node.expanded = true; node.loading = true;
        node.seq = nextSeq++;
        Load load; load.seq = node.seq;
        loads.insert(node.key, load); // one pending entry per visible node, bounded by kMaxRows
        changed(row, {ExpandedRole, LoadingRole});
        pumpLoads();
    }
    void pumpLoads() {
        // Canceled OS calls still occupy their slot until completion. Do not create a future,
        // watcher or QThreadPool job for a waiting node; newest current work goes first.
        while(inFlight<kMaxConcurrentLoads) {
            auto selected=loads.end();
            for(auto it=loads.begin();it!=loads.end();++it)
                if(!it->started && (selected==loads.end() || it->seq>selected->seq))selected=it;
            if(selected==loads.end())return;
            const QString key=selected.key();
            const int row=indexOfKey(key);
            if(row<0){loads.erase(selected);continue;}
            selected->started=true;
            const auto token=selected->stop.get_token();
            const quint64 seq=selected->seq,startEpoch=epoch;
            const auto path=nativePath(rows[std::size_t(row)].path);
            auto* watcher=new QFutureWatcher<glancelot::FolderListing>(q);
            ++inFlight;
            QObject::connect(watcher,&QFutureWatcher<glancelot::FolderListing>::finished,q,[this,watcher,key,seq,startEpoch]{
                auto listing=watcher->result();watcher->deleteLater();--inFlight;
                applyListing(key,seq,startEpoch,std::move(listing));
                pumpLoads();
            });
            watcher->setFuture(QtConcurrent::run(pool,[lister=lister,path,token]{return lister(path,token);}));
        }
    }
    void applyListing(const QString& key, quint64 seq, quint64 startEpoch, glancelot::FolderListing listing) {
        QString finishedPath;
        const int row = startEpoch == epoch ? indexOfKey(key) : -1;
        if (row >= 0 && rows[std::size_t(row)].expanded && rows[std::size_t(row)].seq == seq && !listing.cancelled) {
            loads.remove(key);
            Node& node = rows[std::size_t(row)];
            finishedPath = node.path;
            node.loading = false;
            if (!listing.accessible) { node.state = Inaccessible; node.expanded = false; }
            else if (listing.folders.empty()) { node.state = Empty; node.expanded = false; }
            else {
                const int room = std::max(0, kMaxRows - int(rows.size()));
                const int wanted = int(std::min<std::size_t>(listing.folders.size(), std::size_t(kMaxChildren)));
                const int take = std::min(wanted, room);
                node.state = (listing.truncated || take < int(listing.folders.size())) ? Truncated : HasChildren;
                std::vector<Node> children;
                children.reserve(std::size_t(take));
                for (int i = 0; i < take; ++i) {
                    Node child;
                    child.path = displayPath(listing.folders[std::size_t(i)]);
                    child.key = keyFor(child.path);
                    child.name = displayPath(listing.folders[std::size_t(i)].filename());
                    child.depth = node.depth + 1;
                    children.push_back(std::move(child));
                }
                if (take == 0) { node.expanded = false; }
                else {
                    q->beginInsertRows({}, row + 1, row + take);
                    rows.insert(rows.begin() + row + 1, children.begin(), children.end());
                    q->endInsertRows();
                    emit q->countChanged();
                }
            }
            changed(row, {ExpandedRole, LoadingRole, StateRole});
            refreshCurrent();
        } else if (row >= 0 && rows[std::size_t(row)].seq == seq && !rows[std::size_t(row)].expanded) {
            // Collapsed while loading; collapse() already cleaned up.
        } else if (row >= 0 && rows[std::size_t(row)].seq == seq && listing.cancelled) {
            loads.remove(key);
            Node& node = rows[std::size_t(row)];
            node.loading = false; node.expanded = false;
            changed(row, {ExpandedRole, LoadingRole});
        }
        if (!finishedPath.isEmpty()) processReveal();
        emit q->loadFinished(finishedPath.isEmpty() ? key : finishedPath);
    }
    void collapseRow(int row) {
        Node& node = rows[std::size_t(row)];
        if (node.loading) {
            const auto it = loads.find(node.key);
            if (it != loads.end()) { it->stop.request_stop(); loads.erase(it); }
            node.loading = false;
        }
        if (node.expanded) {
            int last = row;
            while (last + 1 < int(rows.size()) && rows[std::size_t(last + 1)].depth > node.depth) ++last;
            if (last > row) {
                // Children disappearing may hold loads of their own; they are cancelled with the subtree.
                for (int i = row + 1; i <= last; ++i) {
                    const auto it = loads.find(rows[std::size_t(i)].key);
                    if (it != loads.end()) { it->stop.request_stop(); loads.erase(it); }
                }
                q->beginRemoveRows({}, row + 1, last);
                rows.erase(rows.begin() + row + 1, rows.begin() + last + 1);
                q->endRemoveRows();
                emit q->countChanged();
            }
            node.expanded = false;
        }
        changed(row, {ExpandedRole, LoadingRole});
        refreshCurrent();
    }
    // Expand ancestors of the revealed folder one listed level at a time; stop when the path cannot be followed.
    void processReveal() {
        if (revealKey.isEmpty()) return;
        if (!computer && !isSameOrUnder(revealKey, rootKey)) { revealKey.clear(); return; }
        int best = -1;
        for (int i = 0; i < int(rows.size()); ++i)
            if (isSameOrUnder(revealKey, rows[std::size_t(i)].key) && (best < 0 || rows[std::size_t(i)].depth > rows[std::size_t(best)].depth)) best = i;
        if (best < 0) { revealKey.clear(); return; }
        Node& node = rows[std::size_t(best)];
        if (node.key == revealKey) { revealKey.clear(); refreshCurrent(); return; }
        if (node.loading) return; // its completion calls processReveal() again
        if (node.expanded || node.state == Empty || node.state == Inaccessible || node.state == Truncated) { revealKey.clear(); return; }
        startLoad(best);
    }
};

FolderTreeModel::FolderTreeModel(QObject* parent) : QAbstractListModel(parent), d(std::make_unique<Private>(this)) {}
FolderTreeModel::~FolderTreeModel() = default;
int FolderTreeModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(d->rows.size()); }
int FolderTreeModel::count() const { return int(d->rows.size()); }
int FolderTreeModel::currentRow() const { return d->current; }
QString FolderTreeModel::rootPath() const { return d->rows.empty() ? QString{} : d->rows.front().path; }
bool FolderTreeModel::canRaiseRoot() const { return d->canRaise; }
int FolderTreeModel::pendingLoads() const { return d->inFlight; }
void FolderTreeModel::setLister(Lister lister) { d->lister = std::move(lister); }
QString FolderTreeModel::pathAt(int row) const { return row >= 0 && row < count() ? d->rows[std::size_t(row)].path : QString{}; }
QVariantMap FolderTreeModel::rowInfo(int row) const {
    if(row<0 || row>=count()) return {};
    const auto& node=d->rows[std::size_t(row)];
    return {{"path",node.path},{"depth",node.depth},{"expanded",node.expanded},
            {"loading",node.loading},{"isCurrent",row==d->current},
            {"expandable",node.state!=Empty && node.state!=Inaccessible}};
}
int FolderTreeModel::parentRow(int row) const {
    if (row <= 0 || row >= count()) return -1;
    const int depth = d->rows[std::size_t(row)].depth;
    for (int i = row - 1; i >= 0; --i) if (d->rows[std::size_t(i)].depth < depth) return i;
    return -1;
}

QVariant FolderTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const auto& node = d->rows[std::size_t(index.row())];
    switch (role) {
    case NameRole: return node.name;
    case PathRole: return node.path;
    case DepthRole: return node.depth;
    case ExpandedRole: return node.expanded;
    case LoadingRole: return node.loading;
    case StateRole: return int(node.state);
    case IsCurrentRole: return index.row() == d->current;
    default: return {};
    }
}

QHash<int, QByteArray> FolderTreeModel::roleNames() const {
    return {{NameRole, "name"}, {PathRole, "path"}, {DepthRole, "depth"}, {ExpandedRole, "expanded"},
            {LoadingRole, "loading"}, {StateRole, "nodeState"}, {IsCurrentRole, "isCurrent"}};
}

void FolderTreeModel::browseComputer() {
    d->computer = true;
    d->cancelAll(); ++d->epoch;
    std::vector<Private::Node> explicitRoots;
    for(const auto& node:d->rows) if(node.depth==0 && node.key.startsWith("//")) {
        Private::Node root; root.path=node.path; root.key=node.key; root.name=node.name; explicitRoots.push_back(std::move(root));
    }
    beginResetModel(); d->rows.clear(); d->current = -1; d->revealKey=d->currentKey;
    // QDir::drives enumerates logical drive names, without listing their contents.
    for (const auto& drive : QDir::drives()) {
        Private::Node node; node.path = drive.absoluteFilePath(); node.key = keyFor(node.path);
        node.name = QDir::toNativeSeparators(node.path); d->rows.push_back(std::move(node));
    }
    for(auto& root:explicitRoots)d->rows.push_back(std::move(root));
    d->rootKey.clear(); d->canRaise = false;
    endResetModel(); emit countChanged(); emit rootChanged(); emit currentRowChanged();
    if (d->rows.size() == 1) d->startLoad(0);
    d->refreshCurrent(); d->processReveal();
}

void FolderTreeModel::sync(const glancelot::NavigationSnapshot& snapshot) {
    const QString root = snapshot.root.empty() ? QString{} : displayPath(snapshot.root);
    const QString rootKey = keyFor(root);
    const bool rootChanged=!d->computer && rootKey!=d->rootKey;
    if (rootChanged && !d->computer) d->resetRoot(snapshot.root);
    const QString directory = snapshot.directory.empty() ? QString{} : keyFor(displayPath(snapshot.directory));
    if (d->computer && !directory.isEmpty()) {
        bool covered=false;
        for (const auto& node:d->rows) if(node.depth==0 && isSameOrUnder(directory,node.key)) { covered=true; break; }
        // An explicitly opened UNC share may not have a mapped drive letter. Retain it as a location.
        if(!covered && !snapshot.root.empty() && count()<kMaxRows) {
            Private::Node node; node.path=displayPath(snapshot.root); node.key=keyFor(node.path); node.name=node.path;
            beginInsertRows({},count(),count()); d->rows.push_back(std::move(node)); endInsertRows(); emit countChanged();
        }
    }
    if (rootChanged || directory != d->currentKey) {
        d->currentKey = directory;
        d->revealKey = directory;
        d->refreshCurrent();
        d->processReveal();
    }
}

void FolderTreeModel::expand(int row) {
    if (row < 0 || row >= count()) return;
    auto& node = d->rows[std::size_t(row)];
    if (node.expanded || node.loading) return;
    d->startLoad(row);
}
void FolderTreeModel::collapse(int row) {
    if (row < 0 || row >= count()) return;
    d->collapseRow(row);
}
void FolderTreeModel::toggle(int row) {
    if (row < 0 || row >= count()) return;
    const auto& node = d->rows[std::size_t(row)];
    if (node.expanded || node.loading) collapse(row); else expand(row);
}
