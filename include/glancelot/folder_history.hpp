#pragma once
#include <QObject>
#include <QString>
#include <optional>
#include <memory>

// Reversible folder navigation history stack (FOLDER-HISTORY-01 / F001 / NAV-01 / NAV-08 / UX-13).
// Contract (frozen by the lead; change only through the lead):
//  * Manages back and forward stacks of visited folders.
//  * Distinguishes folder visits from intra-folder image stepping: stepping between images
//    updates the current folder's remembered photo/selection, but does not push new history entries.
//  * Navigating to a new folder pushes the previous folder onto the back stack and clears the forward stack.
//  * Bounded capacity (default 50 entries) prevents unbounded memory growth.
//  * Moving back/forward returns the target entry (directory, remembered image, selected item, and scroll index).
//  * Missing/stale directories can be skipped or handled cleanly without corrupting the navigation stacks.

struct FolderHistoryEntry {
    QString directory;
    QString filePath;
    QString selectedPath;
    int scrollPosition{0};

    bool operator==(const FolderHistoryEntry& other) const {
        return directory == other.directory &&
               filePath == other.filePath &&
               selectedPath == other.selectedPath &&
               scrollPosition == other.scrollPosition;
    }
};

class FolderHistory : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY canGoBackChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY canGoForwardChanged)
    Q_PROPERTY(QString backDirectory READ backDirectory NOTIFY historyChanged)
    Q_PROPERTY(QString forwardDirectory READ forwardDirectory NOTIFY historyChanged)
    Q_PROPERTY(QString currentDirectory READ currentDirectory NOTIFY currentDirectoryChanged)
    Q_PROPERTY(int backCount READ backCount NOTIFY historyChanged)
    Q_PROPERTY(int forwardCount READ forwardCount NOTIFY historyChanged)

public:
    explicit FolderHistory(QObject* parent = nullptr);
    ~FolderHistory() override;

    bool canGoBack() const;
    bool canGoForward() const;

    QString backDirectory() const;
    QString forwardDirectory() const;
    QString currentDirectory() const;

    int backCount() const;
    int forwardCount() const;

    int capacity() const;
    void setCapacity(int maxEntries);

    // Records a new folder visit. If `directory` matches the current folder,
    // it simply updates the current folder's state rather than pushing a duplicate.
    // If it is a new folder, the current folder is pushed onto the back stack and
    // the forward stack is cleared.
    void recordVisit(const QString& directory,
                     const QString& filePath = QString(),
                     const QString& selectedPath = QString(),
                     int scrollPosition = 0);

    // Updates state (preview file, selected path, scroll index) of the currently active folder.
    void updateCurrent(const QString& filePath,
                       const QString& selectedPath = QString(),
                       int scrollPosition = -1);

    // Pops previous entry from back stack, pushes current to forward stack, and returns the target entry.
    std::optional<FolderHistoryEntry> goBack();

    // Pops next entry from forward stack, pushes current to back stack, and returns the target entry.
    std::optional<FolderHistoryEntry> goForward();

    // Peeks at current entry.
    std::optional<FolderHistoryEntry> currentEntry() const;

    // Clears all history.
    Q_INVOKABLE void clear();

    // QML-invokable navigation actions
    Q_INVOKABLE bool navigateBack();
    Q_INVOKABLE bool navigateForward();

signals:
    void canGoBackChanged();
    void canGoForwardChanged();
    void currentDirectoryChanged();
    void historyChanged();
    // Emitted when navigateBack() or navigateForward() requests navigation to an entry.
    void navigationRequested(const QString& directory, const QString& filePath, const QString& selectedPath, int scrollPosition);

private:
    struct Private;
    std::unique_ptr<Private> d;
};
