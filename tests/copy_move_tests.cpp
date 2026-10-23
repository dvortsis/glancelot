#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QThread>
#include <QCryptographicHash>
#include <QStorageInfo>
#include <atomic>
#include "glancelot/copy_move_service.hpp"
#include "glancelot/viewer_controller.hpp"
#include "copy_safety.hpp"

class CopyMoveTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testSingleFileCopy();
    void testSingleFileMove();
    void testCollisionPolicies();
    void testDirectoryRecursiveTransfer();
    void testPauseResumeCancel();
    void testClipboardCutCopyPaste();
    void testViewerControllerReconciliation();

    // COPY-SAFETY-01: Overwrite never destroys the existing target
    void testOverwriteTemporaryNaming();
    void testOverwriteReplacesTargetOnlyAfterCompleteCopy();
    void testOverwriteSourceReadFailureKeepsTarget();
    void testOverwriteShrinkingSourceKeepsTarget();
    void testOverwriteWriteFailureKeepsTarget();
    void testOverwriteSyncFailureKeepsTarget();
    void testOverwriteCancelMidCopyKeepsTarget();
    void testOverwriteReplaceFailureKeepsTargetAndSource();
    void testOverwriteRemovesCrashLeftover();
    void testOverwriteCrashLeftoverWithFailedCopyKeepsTarget();
    void testOverwriteMoveSameVolume();
    void testOverwriteMoveCrossVolume();
    void testOverwriteReadOnlyTargetKeepsTarget();
    void testOverwriteOntoItselfKeepsFile();

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;
};

void CopyMoveTests::initTestCase() {
    m_tempDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tempDir->isValid());
}

void CopyMoveTests::testSingleFileCopy() {
    QDir rootDir(m_tempDir->path());
    const QString srcDir = rootDir.filePath(QStringLiteral("src_copy"));
    const QString dstDir = rootDir.filePath(QStringLiteral("dst_copy"));
    QDir().mkpath(srcDir);
    QDir().mkpath(dstDir);

    const QString srcFile = QDir(srcDir).filePath(QStringLiteral("photo.jpg"));
    const QByteArray testData(128 * 1024, 'X');
    {
        QFile f(srcFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(testData);
        f.close();
    }

    glancelot::CopyMoveService service;
    QSignalSpy batchSpy(&service, &glancelot::CopyMoveService::batchFinished);
    QSignalSpy mutatedSpy(&service, &glancelot::CopyMoveService::pathsMutated);

    const quint64 batchId = service.enqueueTransfer({srcFile}, dstDir,
                                                    glancelot::CopyMoveService::TransferMode::Copy,
                                                    glancelot::CopyMoveService::CollisionPolicy::RenameBoth);
    QVERIFY(batchId > 0);

    QVERIFY(batchSpy.wait(5000));
    QCOMPARE(batchSpy.count(), 1);
    QCOMPARE(batchSpy.at(0).at(0).toULongLong(), batchId);
    QCOMPARE(batchSpy.at(0).at(1).toInt(), 1); // successfulCount
    QCOMPARE(batchSpy.at(0).at(2).toInt(), 0); // failedCount

    // Source must still exist
    QVERIFY(QFile::exists(srcFile));
    // Destination must exist with matching size
    const QString dstFile = QDir(dstDir).filePath(QStringLiteral("photo.jpg"));
    QVERIFY(QFile::exists(dstFile));
    QCOMPARE(QFileInfo(dstFile).size(), static_cast<qint64>(testData.size()));

    QCOMPARE(mutatedSpy.count(), 1);
    QCOMPARE(mutatedSpy.at(0).at(2).toBool(), false); // isMove == false
}

void CopyMoveTests::testSingleFileMove() {
    QDir rootDir(m_tempDir->path());
    const QString srcDir = rootDir.filePath(QStringLiteral("src_move"));
    const QString dstDir = rootDir.filePath(QStringLiteral("dst_move"));
    QDir().mkpath(srcDir);
    QDir().mkpath(dstDir);

    const QString srcFile = QDir(srcDir).filePath(QStringLiteral("document.pdf"));
    const QByteArray testData(64 * 1024, 'M');
    {
        QFile f(srcFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(testData);
        f.close();
    }

    glancelot::CopyMoveService service;
    QSignalSpy batchSpy(&service, &glancelot::CopyMoveService::batchFinished);
    QSignalSpy mutatedSpy(&service, &glancelot::CopyMoveService::pathsMutated);

    const quint64 batchId = service.enqueueTransfer({srcFile}, dstDir,
                                                    glancelot::CopyMoveService::TransferMode::Move,
                                                    glancelot::CopyMoveService::CollisionPolicy::RenameBoth);
    QVERIFY(batchId > 0);

    QVERIFY(batchSpy.wait(5000));
    QCOMPARE(batchSpy.count(), 1);
    QCOMPARE(batchSpy.at(0).at(1).toInt(), 1); // successfulCount

    // Source must NO LONGER exist
    QVERIFY(!QFile::exists(srcFile));
    // Destination must exist with matching size
    const QString dstFile = QDir(dstDir).filePath(QStringLiteral("document.pdf"));
    QVERIFY(QFile::exists(dstFile));
    QCOMPARE(QFileInfo(dstFile).size(), static_cast<qint64>(testData.size()));

    QCOMPARE(mutatedSpy.count(), 1);
    QCOMPARE(mutatedSpy.at(0).at(2).toBool(), true); // isMove == true
}

void CopyMoveTests::testCollisionPolicies() {
    QDir rootDir(m_tempDir->path());
    const QString baseDir = rootDir.filePath(QStringLiteral("collisions"));
    const QString srcDir = QDir(baseDir).filePath(QStringLiteral("src"));
    const QString dstDir = QDir(baseDir).filePath(QStringLiteral("dst"));
    QDir().mkpath(srcDir);
    QDir().mkpath(dstDir);

    const QString testFileName = QStringLiteral("item.txt");
    const QString srcFile = QDir(srcDir).filePath(testFileName);
    const QString dstFile = QDir(dstDir).filePath(testFileName);

    // Initial destination content
    {
        QFile f(dstFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("ORIGINAL_DESTINATION");
        f.close();
    }

    // Source content
    {
        QFile f(srcFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("NEW_SOURCE_CONTENT");
        f.close();
    }

    glancelot::CopyMoveService service;

    // 1. Skip Policy
    {
        QSignalSpy batchSpy(&service, &glancelot::CopyMoveService::batchFinished);
        service.enqueueTransfer({srcFile}, dstDir,
                                glancelot::CopyMoveService::TransferMode::Copy,
                                glancelot::CopyMoveService::CollisionPolicy::Skip);
        QVERIFY(batchSpy.wait(5000));

        // Destination should remain unchanged
        QFile f(dstFile);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("ORIGINAL_DESTINATION"));
    }

    // 2. Overwrite Policy
    {
        QSignalSpy batchSpy(&service, &glancelot::CopyMoveService::batchFinished);
        service.enqueueTransfer({srcFile}, dstDir,
                                glancelot::CopyMoveService::TransferMode::Copy,
                                glancelot::CopyMoveService::CollisionPolicy::Overwrite);
        QVERIFY(batchSpy.wait(5000));

        // Destination should now have source content
        QFile f(dstFile);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("NEW_SOURCE_CONTENT"));
    }

    // 3. RenameBoth Policy
    {
        // Set dstFile back to original
        {
            QFile f(dstFile);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("ORIGINAL_DESTINATION");
            f.close();
        }

        QSignalSpy batchSpy(&service, &glancelot::CopyMoveService::batchFinished);
        service.enqueueTransfer({srcFile}, dstDir,
                                glancelot::CopyMoveService::TransferMode::Copy,
                                glancelot::CopyMoveService::CollisionPolicy::RenameBoth);
        QVERIFY(batchSpy.wait(5000));

        // Original dstFile still exists with original content
        {
            QFile f(dstFile);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), QByteArray("ORIGINAL_DESTINATION"));
        }

        // New candidate file exists: item (2).txt
        const QString renamedDst = QDir(dstDir).filePath(QStringLiteral("item (2).txt"));
        QVERIFY(QFile::exists(renamedDst));
        {
            QFile f(renamedDst);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), QByteArray("NEW_SOURCE_CONTENT"));
        }
    }
}

void CopyMoveTests::testDirectoryRecursiveTransfer() {
    QDir rootDir(m_tempDir->path());
    const QString srcDir = rootDir.filePath(QStringLiteral("tree_src"));
    const QString subDir = QDir(srcDir).filePath(QStringLiteral("nested"));
    const QString dstDir = rootDir.filePath(QStringLiteral("tree_dst"));
    QDir().mkpath(subDir);
    QDir().mkpath(dstDir);

    const QString file1 = QDir(srcDir).filePath(QStringLiteral("top.txt"));
    const QString file2 = QDir(subDir).filePath(QStringLiteral("leaf.txt"));
    {
        QFile f(file1);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("TOP_DATA");
    }
    {
        QFile f(file2);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("LEAF_DATA");
    }

    glancelot::CopyMoveService service;

    // Test recursive copy
    {
        QSignalSpy batchSpy(&service, &glancelot::CopyMoveService::batchFinished);
        service.enqueueTransfer({srcDir}, dstDir,
                                glancelot::CopyMoveService::TransferMode::Copy,
                                glancelot::CopyMoveService::CollisionPolicy::RenameBoth);
        QVERIFY(batchSpy.wait(5000));

        const QString copiedRootDir = QDir(dstDir).filePath(QStringLiteral("tree_src"));
        QVERIFY(QDir(copiedRootDir).exists());
        QVERIFY(QFile::exists(QDir(copiedRootDir).filePath(QStringLiteral("top.txt"))));
        QVERIFY(QFile::exists(QDir(copiedRootDir).filePath(QStringLiteral("nested/leaf.txt"))));
        // Source should still exist
        QVERIFY(QFile::exists(file1));
        QVERIFY(QFile::exists(file2));
    }
}

void CopyMoveTests::testPauseResumeCancel() {
    QDir rootDir(m_tempDir->path());
    const QString srcDir = rootDir.filePath(QStringLiteral("pause_src"));
    const QString dstDir = rootDir.filePath(QStringLiteral("pause_dst"));
    QDir().mkpath(srcDir);
    QDir().mkpath(dstDir);

    // Create a 5MB file to allow pause/cancel testing
    const QString bigFile = QDir(srcDir).filePath(QStringLiteral("bigfile.bin"));
    {
        QFile f(bigFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray chunk(256 * 1024, 'Z');
        for (int i = 0; i < 20; ++i) {
            f.write(chunk);
        }
    }

    glancelot::CopyMoveService service;
    QSignalSpy pausedSpy(&service, &glancelot::CopyMoveService::pausedChanged);
    QSignalSpy batchSpy(&service, &glancelot::CopyMoveService::batchFinished);

    service.enqueueTransfer({bigFile}, dstDir, glancelot::CopyMoveService::TransferMode::Copy);

    // Pause
    service.pause();
    QVERIFY(service.paused());

    // Resume
    service.resume();
    QVERIFY(!service.paused());

    // Cancel
    service.cancel();

    QVERIFY(batchSpy.wait(5000));
}

void CopyMoveTests::testClipboardCutCopyPaste() {
    QDir rootDir(m_tempDir->path());
    const QString srcDir = rootDir.filePath(QStringLiteral("clip_src"));
    const QString dstDir1 = rootDir.filePath(QStringLiteral("clip_dst1"));
    const QString dstDir2 = rootDir.filePath(QStringLiteral("clip_dst2"));
    QDir().mkpath(srcDir);
    QDir().mkpath(dstDir1);
    QDir().mkpath(dstDir2);

    const QString testFile = QDir(srcDir).filePath(QStringLiteral("clip.txt"));
    {
        QFile f(testFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("CLIPBOARD_CONTENT");
    }

    glancelot::CopyMoveService service;
    QSignalSpy clipSpy(&service, &glancelot::CopyMoveService::clipboardChanged);

    // 1. Mark Copy
    service.markCopy({testFile});
    QCOMPARE(clipSpy.count(), 1);
    QVERIFY(service.hasClipboardTargets());
    QCOMPARE(service.clipboardMode(), QStringLiteral("copy"));
    QCOMPARE(service.clipboardTargets(), QStringList{testFile});

    // Paste to dstDir1
    {
        QSignalSpy batchSpy(&service, &glancelot::CopyMoveService::batchFinished);
        service.pasteTo(dstDir1);
        QVERIFY(batchSpy.wait(5000));
        QVERIFY(QFile::exists(QDir(dstDir1).filePath(QStringLiteral("clip.txt"))));
        // Clipboard remains intact for copy mode
        QVERIFY(service.hasClipboardTargets());
    }

    // 2. Mark Cut
    service.markCut({testFile});
    QVERIFY(service.hasClipboardTargets());
    QCOMPARE(service.clipboardMode(), QStringLiteral("cut"));

    // Paste to dstDir2 (Move)
    {
        QSignalSpy batchSpy(&service, &glancelot::CopyMoveService::batchFinished);
        service.pasteTo(dstDir2);
        QVERIFY(batchSpy.wait(5000));
        QVERIFY(QFile::exists(QDir(dstDir2).filePath(QStringLiteral("clip.txt"))));
        // Source should now be moved
        QVERIFY(!QFile::exists(testFile));
        // Clipboard should be cleared after cut paste
        QVERIFY(!service.hasClipboardTargets());
    }
}

void CopyMoveTests::testViewerControllerReconciliation() {
    QDir rootDir(m_tempDir->path());
    const QString srcDir = rootDir.filePath(QStringLiteral("recon_src"));
    const QString dstDir = rootDir.filePath(QStringLiteral("recon_dst"));
    QDir().mkpath(srcDir);
    QDir().mkpath(dstDir);

    const QString imgFile = QDir(srcDir).filePath(QStringLiteral("sample.jpg"));
    {
        QFile f(imgFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("DUMMY_IMAGE_DATA");
    }

    ViewerController controller;
    controller.open(imgFile);
    QCOMPARE(QDir::cleanPath(controller.filePath()), QDir::cleanPath(imgFile));

    QVERIFY(controller.copyMoveService() != nullptr);
    QSignalSpy batchSpy(controller.copyMoveService(), &glancelot::CopyMoveService::batchFinished);

    controller.copyMoveService()->enqueueTransfer({imgFile}, dstDir, glancelot::CopyMoveService::TransferMode::Move);
    QVERIFY(batchSpy.wait(5000));

    // Verify destination exists and source is gone
    const QString dstFile = QDir(dstDir).filePath(QStringLiteral("sample.jpg"));
    QVERIFY(QFile::exists(dstFile));
    QVERIFY(!QFile::exists(imgFile));

    // Controller's filePath must be updated to the new destination path!
    QCOMPARE(QDir::cleanPath(controller.filePath()), QDir::cleanPath(dstFile));
}

// ---- COPY-SAFETY-01 ------------------------------------------------------------------------------------------

namespace {

using glancelot::CopyMoveService;
namespace copy_safety = glancelot::copy_safety;

// Deterministic, non-repeating bytes so a truncated or mixed-up file never matches by accident.
QByteArray makeData(int size, quint32 seed) {
    QByteArray data(size, Qt::Uninitialized);
    quint32 state = seed * 2654435761u + 1u;
    for (int i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        data[i] = static_cast<char>(state >> 24);
    }
    return data;
}

bool writeBytes(const QString& path, const QByteArray& data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

QByteArray sha256Of(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray("<missing>");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    return hash.result().toHex();
}

QStringList temporariesIn(const QString& dir) {
    QStringList found;
    const QStringList names = QDir(dir).entryList(QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QString& name : names) {
        if (copy_safety::isTemporaryName(name)) found.append(name);
    }
    return found;
}

// Clears the process-wide fault hook when a test scope ends, even after a failed QVERIFY.
struct FaultHookScope {
    explicit FaultHookScope(copy_safety::FaultHook hook) { copy_safety::setFaultHookForTesting(std::move(hook)); }
    ~FaultHookScope() { copy_safety::setFaultHookForTesting({}); }
};

struct BatchOutcome {
    bool finished{false};
    int succeeded{-1};
    int failed{-1};
    QString lastError;
};

BatchOutcome runTransfer(CopyMoveService& service, const QString& source, const QString& destinationDir,
                         CopyMoveService::TransferMode mode,
                         CopyMoveService::CollisionPolicy policy = CopyMoveService::CollisionPolicy::Overwrite) {
    QSignalSpy batchSpy(&service, &CopyMoveService::batchFinished);
    QSignalSpy itemSpy(&service, &CopyMoveService::itemFinished);
    service.enqueueTransfer({source}, destinationDir, mode, policy);
    BatchOutcome outcome;
    outcome.finished = batchSpy.wait(15000);
    if (outcome.finished) {
        outcome.succeeded = batchSpy.at(0).at(1).toInt();
        outcome.failed = batchSpy.at(0).at(2).toInt();
    }
    for (const auto& args : itemSpy) {
        if (!args.at(2).toBool()) outcome.lastError = args.at(3).toString();
    }
    return outcome;
}

// A folder with "target.bin" (the user's existing file) and a source folder with a different "target.bin".
struct OverwriteFixture {
    QString targetDir;
    QString sourceDir;
    QString target;
    QString source;
    QByteArray targetHash;
    QByteArray sourceHash;
};

bool makeFixture(const QString& root, const QString& name, OverwriteFixture& fx, int sourceSize = 1024 * 1024,
                 const QString& sourceRoot = QString()) {
    fx.targetDir = QDir(root).filePath(name + QStringLiteral("/dst"));
    fx.sourceDir = QDir(sourceRoot.isEmpty() ? root : sourceRoot).filePath(name + QStringLiteral("/src"));
    if (!QDir().mkpath(fx.targetDir) || !QDir().mkpath(fx.sourceDir)) return false;
    fx.target = QDir(fx.targetDir).filePath(QStringLiteral("target.bin"));
    fx.source = QDir(fx.sourceDir).filePath(QStringLiteral("target.bin"));
    if (!writeBytes(fx.target, makeData(700 * 1024 + 13, 1)) || !writeBytes(fx.source, makeData(sourceSize, 2))) return false;
    fx.targetHash = sha256Of(fx.target);
    fx.sourceHash = sha256Of(fx.source);
    return fx.targetHash != fx.sourceHash;
}

} // namespace

void CopyMoveTests::testOverwriteTemporaryNaming() {
    const QString target = QDir(m_tempDir->path()).filePath(QStringLiteral("naming/photo [1].jpg"));
    const QString temp = copy_safety::temporaryPathFor(target);
    QCOMPARE(QFileInfo(temp).absolutePath(), QFileInfo(target).absolutePath());
    QVERIFY(copy_safety::isTemporaryName(QFileInfo(temp).fileName()));
    QVERIFY(temp != copy_safety::temporaryPathFor(target));

    QVERIFY(copy_safety::isTemporaryName(QStringLiteral(".a.jpg.glancelot-0123456789abcdef.part")));
    QVERIFY(!copy_safety::isTemporaryName(QStringLiteral("a.jpg")));
    QVERIFY(!copy_safety::isTemporaryName(QStringLiteral("a.jpg.glancelot-0123456789abcdef.part")));
    QVERIFY(!copy_safety::isTemporaryName(QStringLiteral(".a.jpg.glancelot-0123456789ABCDEF.part")));
    QVERIFY(!copy_safety::isTemporaryName(QStringLiteral(".a.jpg.glancelot-0123.part")));
    QVERIFY(!copy_safety::isTemporaryName(QStringLiteral(".a.jpg.glancelot-0123456789abcdef.part.jpg")));
}

void CopyMoveTests::testOverwriteReplacesTargetOnlyAfterCompleteCopy() {
    OverwriteFixture fx;
    QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_success"), fx));

    // Inspect the target after every chunk: a crash at any of these points would leave it intact.
    std::atomic<int> chunks{0};
    std::atomic<bool> targetChangedDuringCopy{false};
    std::atomic<bool> temporaryMissingDuringCopy{false};
    const QByteArray originalHash = fx.targetHash;
    const QString target = fx.target;
    const QString targetDir = fx.targetDir;
    FaultHookScope hook([&](copy_safety::FaultPoint point, const QString&, qint64) {
        if (point == copy_safety::FaultPoint::AfterChunk) {
            ++chunks;
            if (sha256Of(target) != originalHash) targetChangedDuringCopy = true;
            if (temporariesIn(targetDir).size() != 1) temporaryMissingDuringCopy = true;
        }
        return false;
    });

    CopyMoveService service;
    QSignalSpy mutatedSpy(&service, &CopyMoveService::pathsMutated);
    const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Copy);
    QVERIFY(outcome.finished);
    QCOMPARE(outcome.succeeded, 1);
    QCOMPARE(outcome.failed, 0);
    QVERIFY(chunks.load() > 1);
    QVERIFY(!targetChangedDuringCopy.load());
    QVERIFY(!temporaryMissingDuringCopy.load());
    QCOMPARE(sha256Of(fx.target), fx.sourceHash);
    QCOMPARE(sha256Of(fx.source), fx.sourceHash);
    QCOMPARE(temporariesIn(fx.targetDir), QStringList());
    QCOMPARE(service.transferredBytes(), QFileInfo(fx.source).size());
    QCOMPARE(mutatedSpy.count(), 1);
    QCOMPARE(mutatedSpy.at(0).at(1).toStringList(), QStringList{QDir::cleanPath(fx.target)});
}

void CopyMoveTests::testOverwriteSourceReadFailureKeepsTarget() {
    OverwriteFixture fx;
    QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_read_fail"), fx));
    FaultHookScope hook([](copy_safety::FaultPoint point, const QString&, qint64 bytes) {
        return point == copy_safety::FaultPoint::Read && bytes >= 256 * 1024;
    });

    CopyMoveService service;
    const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Copy);
    QVERIFY(outcome.finished);
    QCOMPARE(outcome.succeeded, 0);
    QCOMPARE(outcome.failed, 1);
    QCOMPARE(sha256Of(fx.target), fx.targetHash);
    QCOMPARE(sha256Of(fx.source), fx.sourceHash);
    QCOMPARE(temporariesIn(fx.targetDir), QStringList());
}

void CopyMoveTests::testOverwriteShrinkingSourceKeepsTarget() {
    // A real short read: another program truncates the source while it is being copied.
    OverwriteFixture fx;
    QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_shrinking_source"), fx));
    const QString source = fx.source;
    std::atomic<bool> truncated{false};
    FaultHookScope hook([&](copy_safety::FaultPoint point, const QString&, qint64 bytes) {
        if (point == copy_safety::FaultPoint::Read && bytes >= 128 * 1024 && !truncated.exchange(true)) {
            QFile::resize(source, 200 * 1024);
        }
        return false;
    });

    CopyMoveService service;
    const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Copy);
    QVERIFY(outcome.finished);
    QVERIFY(truncated.load());
    QCOMPARE(outcome.failed, 1);
    QCOMPARE(sha256Of(fx.target), fx.targetHash);
    QCOMPARE(temporariesIn(fx.targetDir), QStringList());
}

void CopyMoveTests::testOverwriteWriteFailureKeepsTarget() {
    // Simulated full volume part-way through the copy.
    OverwriteFixture fx;
    QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_write_fail"), fx));
    FaultHookScope hook([](copy_safety::FaultPoint point, const QString&, qint64 bytes) {
        return point == copy_safety::FaultPoint::Write && bytes >= 512 * 1024;
    });

    CopyMoveService service;
    const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Copy);
    QVERIFY(outcome.finished);
    QCOMPARE(outcome.succeeded, 0);
    QCOMPARE(outcome.failed, 1);
    QCOMPARE(sha256Of(fx.target), fx.targetHash);
    QCOMPARE(sha256Of(fx.source), fx.sourceHash);
    QCOMPARE(temporariesIn(fx.targetDir), QStringList());
}

void CopyMoveTests::testOverwriteSyncFailureKeepsTarget() {
    // Simulated full volume when the written data is flushed to disk.
    OverwriteFixture fx;
    QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_sync_fail"), fx));
    FaultHookScope hook([](copy_safety::FaultPoint point, const QString&, qint64) {
        return point == copy_safety::FaultPoint::Sync;
    });

    CopyMoveService service;
    const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Copy);
    QVERIFY(outcome.finished);
    QCOMPARE(outcome.failed, 1);
    QCOMPARE(sha256Of(fx.target), fx.targetHash);
    QCOMPARE(temporariesIn(fx.targetDir), QStringList());
}

void CopyMoveTests::testOverwriteCancelMidCopyKeepsTarget() {
    OverwriteFixture fx;
    QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_cancel"), fx, 4 * 1024 * 1024));
    CopyMoveService service;
    std::atomic<qint64> cancelledAt{-1};
    FaultHookScope hook([&](copy_safety::FaultPoint point, const QString&, qint64 bytes) {
        if (point == copy_safety::FaultPoint::AfterChunk && bytes >= 1024 * 1024 && cancelledAt.load() < 0) {
            cancelledAt = bytes;
            service.cancel();
        }
        return false;
    });

    const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Copy);
    QVERIFY(outcome.finished);
    QVERIFY(cancelledAt.load() > 0);
    QVERIFY(cancelledAt.load() < QFileInfo(fx.source).size());
    QCOMPARE(outcome.succeeded, 0);
    QCOMPARE(outcome.failed, 1);
    QCOMPARE(sha256Of(fx.target), fx.targetHash);
    QCOMPARE(sha256Of(fx.source), fx.sourceHash);
    QCOMPARE(temporariesIn(fx.targetDir), QStringList());
}

void CopyMoveTests::testOverwriteReplaceFailureKeepsTargetAndSource() {
    // The last step fails (Windows: access denied or a sharing violation on the target).
    FaultHookScope hook([](copy_safety::FaultPoint point, const QString&, qint64) {
        return point == copy_safety::FaultPoint::Replace;
    });
    for (const auto mode : {CopyMoveService::TransferMode::Copy, CopyMoveService::TransferMode::Move}) {
        OverwriteFixture fx;
        QVERIFY(makeFixture(m_tempDir->path(), mode == CopyMoveService::TransferMode::Copy ? QStringLiteral("ow_replace_copy")
                                                                                         : QStringLiteral("ow_replace_move"), fx));
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, mode);
        QVERIFY(outcome.finished);
        QCOMPARE(outcome.failed, 1);
        QVERIFY2(outcome.lastError.contains(QStringLiteral("Original kept")), qPrintable(outcome.lastError));
        QCOMPARE(sha256Of(fx.target), fx.targetHash);
        QCOMPARE(sha256Of(fx.source), fx.sourceHash);
        QCOMPARE(temporariesIn(fx.targetDir), QStringList());
    }
}

void CopyMoveTests::testOverwriteRemovesCrashLeftover() {
    // State after a crash between writing the temporary and replacing the target: the target is the
    // original and a partial temporary sits beside it. The next Overwrite into that folder clears it.
    OverwriteFixture fx;
    QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_crash_leftover"), fx));
    const QString leftover = QDir(fx.targetDir).filePath(QStringLiteral(".target.bin.glancelot-00000000deadbeef.part"));
    const QString otherLeftover = QDir(fx.targetDir).filePath(QStringLiteral(".other.jpg.glancelot-0123456789abcdef.part"));
    QVERIFY(writeBytes(leftover, makeData(300 * 1024, 2)));
    QVERIFY(writeBytes(otherLeftover, makeData(10, 3)));
    // Look-alike user files are never touched.
    const QStringList userFiles = {QStringLiteral(".target.bin.glancelot-1234.part"), QStringLiteral("target.bin.part"),
                                   QStringLiteral(".target.bin.glancelot-00000000deadbeef.part.txt")};
    for (const QString& name : userFiles) QVERIFY(writeBytes(QDir(fx.targetDir).filePath(name), QByteArray("user")));
    QCOMPARE(sha256Of(fx.target), fx.targetHash);

    CopyMoveService service;
    const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Copy);
    QVERIFY(outcome.finished);
    QCOMPARE(outcome.succeeded, 1);
    QCOMPARE(sha256Of(fx.target), fx.sourceHash);
    QCOMPARE(temporariesIn(fx.targetDir), QStringList());
    for (const QString& name : userFiles) QVERIFY2(QFile::exists(QDir(fx.targetDir).filePath(name)), qPrintable(name));
}

void CopyMoveTests::testOverwriteCrashLeftoverWithFailedCopyKeepsTarget() {
    OverwriteFixture fx;
    QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_crash_then_fail"), fx));
    const QString leftover = QDir(fx.targetDir).filePath(QStringLiteral(".target.bin.glancelot-00000000deadbeef.part"));
    QVERIFY(writeBytes(leftover, makeData(1024 * 1024, 2)));
    FaultHookScope hook([](copy_safety::FaultPoint point, const QString&, qint64 bytes) {
        return point == copy_safety::FaultPoint::Read && bytes > 0;
    });

    CopyMoveService service;
    const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Copy);
    QVERIFY(outcome.finished);
    QCOMPARE(outcome.failed, 1);
    QCOMPARE(sha256Of(fx.target), fx.targetHash);
    QCOMPARE(temporariesIn(fx.targetDir), QStringList());
}

void CopyMoveTests::testOverwriteMoveSameVolume() {
    // Success: one atomic rename replaces the target and the source is gone.
    {
        OverwriteFixture fx;
        QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_move_same"), fx));
        CopyMoveService service;
        QSignalSpy mutatedSpy(&service, &CopyMoveService::pathsMutated);
        const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Move);
        QVERIFY(outcome.finished);
        QCOMPARE(outcome.succeeded, 1);
        QCOMPARE(sha256Of(fx.target), fx.sourceHash);
        QVERIFY(!QFile::exists(fx.source));
        QCOMPARE(temporariesIn(fx.targetDir), QStringList());
        QCOMPARE(mutatedSpy.count(), 1);
        QCOMPARE(mutatedSpy.at(0).at(2).toBool(), true);
    }
    // Failure of every replace: the rename and the copy fallback both fail and nothing changes.
    {
        OverwriteFixture fx;
        QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_move_same_fail"), fx));
        FaultHookScope hook([](copy_safety::FaultPoint point, const QString&, qint64) {
            return point == copy_safety::FaultPoint::Replace;
        });
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Move);
        QVERIFY(outcome.finished);
        QCOMPARE(outcome.failed, 1);
        QCOMPARE(sha256Of(fx.target), fx.targetHash);
        QCOMPARE(sha256Of(fx.source), fx.sourceHash);
        QCOMPARE(temporariesIn(fx.targetDir), QStringList());
    }
}

void CopyMoveTests::testOverwriteMoveCrossVolume() {
    // A folder on a different volume: GLANCELOT_CROSS_VOLUME_DIR (any platform), else /dev/shm on Linux.
    QString otherRoot = qEnvironmentVariable("GLANCELOT_CROSS_VOLUME_DIR");
#ifdef Q_OS_LINUX
    if (otherRoot.isEmpty() && QDir(QStringLiteral("/dev/shm")).exists()) otherRoot = QStringLiteral("/dev/shm");
#endif
    if (otherRoot.isEmpty()) QSKIP("Set GLANCELOT_CROSS_VOLUME_DIR to a folder on another volume to run this test.");
    QTemporaryDir otherVolume(QDir(otherRoot).filePath(QStringLiteral("glancelot-copy-safety-XXXXXX")));
    if (!otherVolume.isValid()) QSKIP("Cannot create a folder on the other volume.");
    if (QStorageInfo(otherVolume.path()).device() == QStorageInfo(m_tempDir->path()).device()) {
        QSKIP("The other folder is on the same volume as the test folder.");
    }

    // Success: verified copy into a temporary on the target volume, replace, then the source is deleted.
    {
        OverwriteFixture fx;
        QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_move_cross"), fx, 1024 * 1024, otherVolume.path()));
        std::atomic<bool> sourceGoneBeforeReplace{false};
        const QString source = fx.source;
        FaultHookScope hook([&](copy_safety::FaultPoint point, const QString&, qint64) {
            if (point == copy_safety::FaultPoint::Replace && !QFile::exists(source)) sourceGoneBeforeReplace = true;
            return false;
        });
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Move);
        QVERIFY(outcome.finished);
        QCOMPARE(outcome.succeeded, 1);
        QVERIFY(!sourceGoneBeforeReplace.load());
        QCOMPARE(sha256Of(fx.target), fx.sourceHash);
        QVERIFY(!QFile::exists(fx.source));
        QCOMPARE(temporariesIn(fx.targetDir), QStringList());
    }
    // Failure part-way: target and source both intact.
    {
        OverwriteFixture fx;
        QVERIFY(makeFixture(m_tempDir->path(), QStringLiteral("ow_move_cross_fail"), fx, 1024 * 1024, otherVolume.path()));
        FaultHookScope hook([](copy_safety::FaultPoint point, const QString&, qint64 bytes) {
            return point == copy_safety::FaultPoint::Read && bytes >= 512 * 1024;
        });
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, CopyMoveService::TransferMode::Move);
        QVERIFY(outcome.finished);
        QCOMPARE(outcome.failed, 1);
        QCOMPARE(sha256Of(fx.target), fx.targetHash);
        QCOMPARE(sha256Of(fx.source), fx.sourceHash);
        QCOMPARE(temporariesIn(fx.targetDir), QStringList());
        QCOMPARE(temporariesIn(fx.sourceDir), QStringList());
    }
}

void CopyMoveTests::testOverwriteReadOnlyTargetKeepsTarget() {
    for (const auto mode : {CopyMoveService::TransferMode::Copy, CopyMoveService::TransferMode::Move}) {
        OverwriteFixture fx;
        QVERIFY(makeFixture(m_tempDir->path(), mode == CopyMoveService::TransferMode::Copy ? QStringLiteral("ow_readonly_copy")
                                                                                         : QStringLiteral("ow_readonly_move"), fx));
        const QFileDevice::Permissions original = QFile::permissions(fx.target);
        QVERIFY(QFile::setPermissions(fx.target, QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup | QFileDevice::ReadOther));
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, fx.source, fx.targetDir, mode);
        const QByteArray targetHashAfter = sha256Of(fx.target);
        const bool targetStillReadOnly = !QFileInfo(fx.target).permission(QFileDevice::WriteOwner);
        QFile::setPermissions(fx.target, original);
        QVERIFY(outcome.finished);
        QCOMPARE(outcome.failed, 1);
        QCOMPARE(outcome.lastError, QStringLiteral("Destination file is read-only."));
        QCOMPARE(targetHashAfter, fx.targetHash);
        QVERIFY(targetStillReadOnly);
        QCOMPARE(sha256Of(fx.source), fx.sourceHash);
        QCOMPARE(temporariesIn(fx.targetDir), QStringList());
    }
}

void CopyMoveTests::testOverwriteOntoItselfKeepsFile() {
    // Copy or move a file into its own folder with Overwrite: the "target" is the source itself.
    for (const auto mode : {CopyMoveService::TransferMode::Copy, CopyMoveService::TransferMode::Move}) {
        const QString dir = QDir(m_tempDir->path()).filePath(mode == CopyMoveService::TransferMode::Copy
                                                                 ? QStringLiteral("ow_self_copy") : QStringLiteral("ow_self_move"));
        QVERIFY(QDir().mkpath(dir));
        const QString file = QDir(dir).filePath(QStringLiteral("self.bin"));
        QVERIFY(writeBytes(file, makeData(300 * 1024, 5)));
        const QByteArray hash = sha256Of(file);
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, file, dir, mode);
        QVERIFY(outcome.finished);
        QCOMPARE(outcome.failed, 1);
        QCOMPARE(sha256Of(file), hash);
        QCOMPARE(temporariesIn(dir), QStringList());
    }
}

QTEST_MAIN(CopyMoveTests)
#include "copy_move_tests.moc"

