#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QThread>
#include "glancelot/copy_move_service.hpp"
#include "glancelot/viewer_controller.hpp"

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

QTEST_MAIN(CopyMoveTests)
#include "copy_move_tests.moc"

