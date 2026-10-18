#pragma once
#include "contracts.hpp"
#include "navigation.hpp"
#include <QAbstractListModel>
#include <functional>
#include <memory>
#include <stop_token>

// Flattened, lazily loaded view of the traversal root's folder hierarchy for the left HUD.
// Contract (frozen by the lead; change only through the lead):
//  * Folder listing never runs on the GUI thread: expanding starts a cancellable background load; collapsing,
//    changing the root or destroying the model cancels it. A result is applied only if its node still exists,
//    is still expanded and belongs to the current root epoch.
//  * At most kMaxChildren folders per parent and kMaxRows rows overall are materialized; a trimmed parent reports
//    NodeState::Truncated. Collapsing discards the children (nothing stale is retained).
//  * At most kMaxConcurrentLoads jobs/futures exist, even when canceled OS calls do not finish.
//    Waiting loads have one entry per materialized node; cancel/root reset removes them before submission.
//  * sync() follows the viewer: it (re)roots at snapshot.root, marks snapshot.directory current and reveals it by
//    expanding its ancestors one level at a time. It never scans more than the levels on that path.
//  * Selecting a folder is not this model's job: the UI calls ViewerController::openFolder(path).
class FolderTreeModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootChanged)
    Q_PROPERTY(bool canRaiseRoot READ canRaiseRoot NOTIFY rootChanged)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentRowChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, DepthRole, ExpandedRole, LoadingRole, StateRole, IsCurrentRole };
    // Unknown: not listed yet (shown as expandable). Empty: listed, no subfolders. Inaccessible: cannot be listed.
    enum NodeState { Unknown, HasChildren, Empty, Inaccessible, Truncated };
    Q_ENUM(NodeState)
    static constexpr int kMaxConcurrentLoads = 2;
    static constexpr int kMaxChildren = 2000;
    static constexpr int kMaxRows = 20000;
    using Lister = std::function<glancelot::FolderListing(const std::filesystem::path&, std::stop_token)>;

    explicit FolderTreeModel(QObject* parent = nullptr);
    ~FolderTreeModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString rootPath() const;
    bool canRaiseRoot() const;
    int currentRow() const;
    int count() const;
    void sync(const glancelot::NavigationSnapshot& snapshot);
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void expand(int row);
    Q_INVOKABLE void collapse(int row);
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE int parentRow(int row) const;
    // Test seams: listings in flight, and an injectable lister (default: glancelot::childFolders).
    int pendingLoads() const;
    void setLister(Lister lister);
signals:
    void rootChanged();
    void currentRowChanged();
    void countChanged();
    // Emitted once per finished (applied or discarded) load; used by tests and by the UI to scroll the current row.
    void loadFinished(const QString& path);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
