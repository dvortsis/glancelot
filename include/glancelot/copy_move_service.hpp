#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

namespace glancelot {

// Asynchronous file copy and move queue service (COPY-MOVE-01 / F020 / META-07 / SYS-06).
// Features:
// - Non-blocking background worker queue.
// - Cross-volume moves verify target size/copy before deleting source (META-07).
// - Pause, resume, and cancel lifecycle controls.
// - Bounded chunked I/O transfer with real-time byte and item progress.
// - Collision policies: RenameBoth, Overwrite, Skip.
// - Clipboard Cut / Copy / Paste routing (Ctrl+X, Ctrl+C, Ctrl+V).
class CopyMoveService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(int totalItems READ totalItems NOTIFY progressChanged)
    Q_PROPERTY(int completedItems READ completedItems NOTIFY progressChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY progressChanged)
    Q_PROPERTY(qint64 transferredBytes READ transferredBytes NOTIFY progressChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString currentItem READ currentItem NOTIFY progressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY progressChanged)
    Q_PROPERTY(bool hasClipboardTargets READ hasClipboardTargets NOTIFY clipboardChanged)
    Q_PROPERTY(QString clipboardMode READ clipboardMode NOTIFY clipboardChanged)

public:
    enum class TransferMode {
        Copy,
        Move
    };
    Q_ENUM(TransferMode)

    enum class CollisionPolicy {
        Skip,
        Overwrite,
        RenameBoth
    };
    Q_ENUM(CollisionPolicy)

    explicit CopyMoveService(QObject* parent = nullptr);
    ~CopyMoveService() override;

    bool active() const;
    bool paused() const;
    int totalItems() const;
    int completedItems() const;
    qint64 totalBytes() const;
    qint64 transferredBytes() const;
    qreal progress() const;
    QString currentItem() const;
    QString statusMessage() const;

    // Clipboard integration
    bool hasClipboardTargets() const;
    QString clipboardMode() const;
    QStringList clipboardTargets() const;

    Q_INVOKABLE quint64 enqueueTransfer(const QStringList& sourcePaths,
                                       const QString& destinationDirectory,
                                       glancelot::CopyMoveService::TransferMode mode,
                                       glancelot::CopyMoveService::CollisionPolicy collision = glancelot::CopyMoveService::CollisionPolicy::RenameBoth);

    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void cancel();

    // Clipboard cut/copy/paste
    Q_INVOKABLE void markCut(const QStringList& paths);
    Q_INVOKABLE void markCopy(const QStringList& paths);
    Q_INVOKABLE void pasteTo(const QString& destinationDirectory);
    Q_INVOKABLE void clearClipboard();

signals:
    void activeChanged();
    void pausedChanged();
    void progressChanged();
    void clipboardChanged();
    void itemStarted(const QString& sourcePath, const QString& destPath);
    void itemFinished(const QString& sourcePath, const QString& destPath, bool success, const QString& error);
    void batchFinished(quint64 batchId, int successfulCount, int failedCount);
    void pathsMutated(const QStringList& sourcePaths, const QStringList& destPaths, bool isMove);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace glancelot
