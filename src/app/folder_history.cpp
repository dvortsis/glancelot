#include "glancelot/folder_history.hpp"
#include <QDir>
#include <QFileInfo>

namespace {
QString normalizePath(const QString& path) {
    if (path.isEmpty()) return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool pathsEqual(const QString& a, const QString& b) {
    const QString na = normalizePath(a);
    const QString nb = normalizePath(b);
#ifdef Q_OS_WIN
    return na.compare(nb, Qt::CaseInsensitive) == 0;
#else
    return na == nb;
#endif
}
} // namespace

struct FolderHistory::Private {
    FolderHistory* q;
    QList<FolderHistoryEntry> backStack;
    QList<FolderHistoryEntry> forwardStack;
    std::optional<FolderHistoryEntry> current;
    int maxCapacity{50};
    bool navigating{false};

    explicit Private(FolderHistory* parent) : q(parent) {}

    void emitChanges(bool backChanged, bool forwardChanged, bool currentChanged) {
        if (backChanged) emit q->canGoBackChanged();
        if (forwardChanged) emit q->canGoForwardChanged();
        if (currentChanged) emit q->currentDirectoryChanged();
        emit q->historyChanged();
    }
};

FolderHistory::FolderHistory(QObject* parent)
    : QObject(parent), d(std::make_unique<Private>(this)) {}

FolderHistory::~FolderHistory() = default;

bool FolderHistory::canGoBack() const {
    return !d->backStack.isEmpty();
}

bool FolderHistory::canGoForward() const {
    return !d->forwardStack.isEmpty();
}

QString FolderHistory::backDirectory() const {
    return d->backStack.isEmpty() ? QString() : d->backStack.last().directory;
}

QString FolderHistory::forwardDirectory() const {
    return d->forwardStack.isEmpty() ? QString() : d->forwardStack.last().directory;
}

QString FolderHistory::currentDirectory() const {
    return d->current.has_value() ? d->current->directory : QString();
}

int FolderHistory::backCount() const {
    return d->backStack.size();
}

int FolderHistory::forwardCount() const {
    return d->forwardStack.size();
}

int FolderHistory::capacity() const {
    return d->maxCapacity;
}

void FolderHistory::setCapacity(int maxEntries) {
    if (maxEntries < 1) maxEntries = 1;
    d->maxCapacity = maxEntries;
    while (d->backStack.size() > d->maxCapacity) {
        d->backStack.removeFirst();
    }
    while (d->forwardStack.size() > d->maxCapacity) {
        d->forwardStack.removeFirst();
    }
    emit historyChanged();
}

void FolderHistory::recordVisit(const QString& directory,
                                 const QString& filePath,
                                 const QString& selectedPath,
                                 int scrollPosition) {
    const QString normalizedDir = normalizePath(directory);
    if (normalizedDir.isEmpty()) return;

    const bool prevCanBack = canGoBack();
    const bool prevCanForward = canGoForward();
    const QString prevCurrentDir = currentDirectory();

    if (d->navigating) {
        if (d->current.has_value() && pathsEqual(d->current->directory, normalizedDir)) {
            d->navigating = false;
            if (!filePath.isEmpty()) d->current->filePath = filePath;
            if (!selectedPath.isEmpty()) d->current->selectedPath = selectedPath;
            if (scrollPosition >= 0) d->current->scrollPosition = scrollPosition;
            return;
        }
        d->navigating = false;
    }

    if (d->current.has_value() && pathsEqual(d->current->directory, normalizedDir)) {
        // Intra-folder update: do not push a duplicate entry onto history.
        if (!filePath.isEmpty()) d->current->filePath = filePath;
        if (!selectedPath.isEmpty()) d->current->selectedPath = selectedPath;
        if (scrollPosition >= 0) d->current->scrollPosition = scrollPosition;
        return;
    }

    if (d->current.has_value()) {
        d->backStack.append(*d->current);
        while (d->backStack.size() > d->maxCapacity) {
            d->backStack.removeFirst();
        }
        // Linear branching: a new navigation clears the forward stack.
        d->forwardStack.clear();
    }

    FolderHistoryEntry newEntry;
    newEntry.directory = normalizedDir;
    newEntry.filePath = filePath;
    newEntry.selectedPath = selectedPath;
    newEntry.scrollPosition = scrollPosition >= 0 ? scrollPosition : 0;
    d->current = newEntry;

    d->emitChanges(canGoBack() != prevCanBack,
                   canGoForward() != prevCanForward,
                   currentDirectory() != prevCurrentDir);
}

void FolderHistory::updateCurrent(const QString& filePath,
                                   const QString& selectedPath,
                                   int scrollPosition) {
    if (!d->current.has_value()) return;
    if (!filePath.isEmpty()) d->current->filePath = filePath;
    if (!selectedPath.isEmpty()) d->current->selectedPath = selectedPath;
    if (scrollPosition >= 0) d->current->scrollPosition = scrollPosition;
}

std::optional<FolderHistoryEntry> FolderHistory::goBack() {
    if (d->backStack.isEmpty()) return std::nullopt;

    const bool prevCanForward = canGoForward();
    const QString prevCurrentDir = currentDirectory();

    if (d->current.has_value()) {
        d->forwardStack.append(*d->current);
        while (d->forwardStack.size() > d->maxCapacity) {
            d->forwardStack.removeFirst();
        }
    }

    const FolderHistoryEntry target = d->backStack.takeLast();
    d->current = target;

    d->emitChanges(true, canGoForward() != prevCanForward, currentDirectory() != prevCurrentDir);
    return target;
}

std::optional<FolderHistoryEntry> FolderHistory::goForward() {
    if (d->forwardStack.isEmpty()) return std::nullopt;

    const bool prevCanBack = canGoBack();
    const QString prevCurrentDir = currentDirectory();

    if (d->current.has_value()) {
        d->backStack.append(*d->current);
        while (d->backStack.size() > d->maxCapacity) {
            d->backStack.removeFirst();
        }
    }

    const FolderHistoryEntry target = d->forwardStack.takeLast();
    d->current = target;

    d->emitChanges(canGoBack() != prevCanBack, true, currentDirectory() != prevCurrentDir);
    return target;
}

std::optional<FolderHistoryEntry> FolderHistory::currentEntry() const {
    return d->current;
}

void FolderHistory::clear() {
    const bool prevCanBack = canGoBack();
    const bool prevCanForward = canGoForward();
    const bool prevHasCurrent = d->current.has_value();

    d->backStack.clear();
    d->forwardStack.clear();
    d->current.reset();
    d->navigating = false;

    d->emitChanges(prevCanBack, prevCanForward, prevHasCurrent);
}

bool FolderHistory::navigateBack() {
    while (!d->backStack.isEmpty()) {
        const FolderHistoryEntry candidate = d->backStack.takeLast();
        // Stale / inaccessible directory check: verify existence if absolute path
        const bool exists = QDir::isAbsolutePath(candidate.directory) ? QDir(candidate.directory).exists() : true;
        if (exists) {
            const bool prevCanForward = canGoForward();
            const QString prevCurrentDir = currentDirectory();
            if (d->current.has_value()) {
                d->forwardStack.append(*d->current);
                while (d->forwardStack.size() > d->maxCapacity) {
                    d->forwardStack.removeFirst();
                }
            }
            d->current = candidate;
            d->navigating = true;
            d->emitChanges(true, canGoForward() != prevCanForward, currentDirectory() != prevCurrentDir);
            emit navigationRequested(candidate.directory, candidate.filePath, candidate.selectedPath, candidate.scrollPosition);
            return true;
        }
        // Stale entry skipped; continue through stack
    }
    d->emitChanges(canGoBack(), canGoForward(), false);
    return false;
}

bool FolderHistory::navigateForward() {
    while (!d->forwardStack.isEmpty()) {
        const FolderHistoryEntry candidate = d->forwardStack.takeLast();
        const bool exists = QDir::isAbsolutePath(candidate.directory) ? QDir(candidate.directory).exists() : true;
        if (exists) {
            const bool prevCanBack = canGoBack();
            const QString prevCurrentDir = currentDirectory();
            if (d->current.has_value()) {
                d->backStack.append(*d->current);
                while (d->backStack.size() > d->maxCapacity) {
                    d->backStack.removeFirst();
                }
            }
            d->current = candidate;
            d->navigating = true;
            d->emitChanges(canGoBack() != prevCanBack, true, currentDirectory() != prevCurrentDir);
            emit navigationRequested(candidate.directory, candidate.filePath, candidate.selectedPath, candidate.scrollPosition);
            return true;
        }
        // Stale entry skipped; continue through stack
    }
    d->emitChanges(canGoBack(), canGoForward(), false);
    return false;
}

