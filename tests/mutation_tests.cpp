#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include "glancelot/mutation_service.hpp"
#include "glancelot/folder_history.hpp"
#include "glancelot/viewer_controller.hpp"

class MutationTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testFileNameValidation();
    void testTargetValidationAndCollisions();
    void testSuggestNewFolderName();
    void testFileRename();
    void testCaseOnlyRename();
    void testDirectoryRename();
    void testCreateFolder();
    void testFolderHistoryReconciliation();
    void testViewerControllerReconciliation();

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;
};

void MutationTests::initTestCase() {
    m_tempDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tempDir->isValid());
}

void MutationTests::testFileNameValidation() {
    QString error;

    // 1. Empty & whitespace-only names
    QVERIFY(!glancelot::MutationService::isValidFileName(QString(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!glancelot::MutationService::isValidFileName(QStringLiteral("   "), &error));
    QVERIFY(!error.isEmpty());

    // 2. Excessively long names (> 255 chars)
    const QString longName = QString(256, QLatin1Char('a'));
    QVERIFY(!glancelot::MutationService::isValidFileName(longName, &error));
    QVERIFY(error.contains(QStringLiteral("255")));

    // 3. ASCII control characters
    QVERIFY(!glancelot::MutationService::isValidFileName(QStringLiteral("file\tname"), &error));
    QVERIFY(!glancelot::MutationService::isValidFileName(QStringLiteral("file\nname"), &error));
    QVERIFY(!glancelot::MutationService::isValidFileName(QStringLiteral("file\x01name"), &error));

    // 4. Forbidden filesystem characters: \ / : * ? " < > |
    static const QString forbiddenChars = QStringLiteral("\\/:*?\"<>|");
    for (const QChar& ch : forbiddenChars) {
        const QString testName = QStringLiteral("file") + ch + QStringLiteral("name.txt");
        QVERIFY2(!glancelot::MutationService::isValidFileName(testName, &error),
                 qPrintable(QStringLiteral("Character %1 should be invalid").arg(ch)));
    }

    // 5. Trailing periods or spaces
    QVERIFY(!glancelot::MutationService::isValidFileName(QStringLiteral("filename."), &error));
    QVERIFY(error.contains(QStringLiteral("period or space")));
    QVERIFY(!glancelot::MutationService::isValidFileName(QStringLiteral("filename "), &error));
    QVERIFY(error.contains(QStringLiteral("period or space")));

    // 6. Windows reserved device names
    const QStringList reservedNames = {
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT9"),
        QStringLiteral("con"), QStringLiteral("aux"), QStringLiteral("Nul.txt"), QStringLiteral("prn.jpg"),
        QStringLiteral("com3.png")
    };
    for (const QString& reserved : reservedNames) {
        QVERIFY2(!glancelot::MutationService::isValidFileName(reserved, &error),
                 qPrintable(QStringLiteral("Name %1 should be recognized as reserved").arg(reserved)));
        QVERIFY(error.contains(QStringLiteral("reserved")));
    }

    // 7. Valid filenames
    QVERIFY(glancelot::MutationService::isValidFileName(QStringLiteral("valid_file.jpg"), &error));
    QVERIFY(glancelot::MutationService::isValidFileName(QStringLiteral("My Trip - 2026 (Summer)"), &error));
    QVERIFY(glancelot::MutationService::isValidFileName(QStringLiteral("photo 1.2.3.png"), &error));
    QVERIFY(glancelot::MutationService::isValidFileName(QStringLiteral("CON-test.txt"), &error));
}

void MutationTests::testTargetValidationAndCollisions() {
    glancelot::MutationService service;
    const QString parent = m_tempDir->path();

    // Create fixture file and folder
    const QString existingFile = QDir(parent).filePath(QStringLiteral("existing_photo.jpg"));
    {
        QFile f(existingFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("photo");
    }

    const QString existingFolder = QDir(parent).filePath(QStringLiteral("existing_folder"));
    QVERIFY(QDir(parent).mkdir(QStringLiteral("existing_folder")));

    // A brand new name is valid and not colliding
    QVariantMap res = service.validateTargetName(parent, QStringLiteral("new_photo.jpg"));
    QVERIFY(res.value(QStringLiteral("valid")).toBool());
    QVERIFY(res.value(QStringLiteral("error")).toString().isEmpty());
    QVERIFY(!res.value(QStringLiteral("isCaseOnly")).toBool());

    // Colliding with existing file
    res = service.validateTargetName(parent, QStringLiteral("existing_photo.jpg"));
    QVERIFY(!res.value(QStringLiteral("valid")).toBool());
    QVERIFY(res.value(QStringLiteral("error")).toString().contains(QStringLiteral("already exists")));

    // Colliding with existing folder
    res = service.validateTargetName(parent, QStringLiteral("existing_folder"));
    QVERIFY(!res.value(QStringLiteral("valid")).toBool());
    QVERIFY(res.value(QStringLiteral("error")).toString().contains(QStringLiteral("already exists")));

    // Same name with currentPath (no change) is valid
    res = service.validateTargetName(parent, QStringLiteral("existing_photo.jpg"), existingFile);
    QVERIFY(res.value(QStringLiteral("valid")).toBool());
    QVERIFY(!res.value(QStringLiteral("isCaseOnly")).toBool());

    // Case-only rename with currentPath is valid and flagged isCaseOnly
    res = service.validateTargetName(parent, QStringLiteral("Existing_Photo.jpg"), existingFile);
    QVERIFY(res.value(QStringLiteral("valid")).toBool());
    QVERIFY(res.value(QStringLiteral("isCaseOnly")).toBool());

    // Case-only rename for folder
    res = service.validateTargetName(parent, QStringLiteral("Existing_Folder"), existingFolder);
    QVERIFY(res.value(QStringLiteral("valid")).toBool());
    QVERIFY(res.value(QStringLiteral("isCaseOnly")).toBool());
}

void MutationTests::testSuggestNewFolderName() {
    glancelot::MutationService service;
    const QString parent = m_tempDir->path();

    // Initially suggests "New Folder"
    QString suggested = service.suggestNewFolderName(parent);
    QCOMPARE(suggested, QStringLiteral("New Folder"));

    // Create "New Folder"
    QVERIFY(QDir(parent).mkdir(QStringLiteral("New Folder")));

    // Now suggests "New Folder (2)"
    suggested = service.suggestNewFolderName(parent);
    QCOMPARE(suggested, QStringLiteral("New Folder (2)"));

    // Create "New Folder (2)"
    QVERIFY(QDir(parent).mkdir(QStringLiteral("New Folder (2)")));

    // Now suggests "New Folder (3)"
    suggested = service.suggestNewFolderName(parent);
    QCOMPARE(suggested, QStringLiteral("New Folder (3)"));

    // Custom base name
    suggested = service.suggestNewFolderName(parent, QStringLiteral("Favorites"));
    QCOMPARE(suggested, QStringLiteral("Favorites"));
    QVERIFY(QDir(parent).mkdir(QStringLiteral("Favorites")));
    suggested = service.suggestNewFolderName(parent, QStringLiteral("Favorites"));
    QCOMPARE(suggested, QStringLiteral("Favorites (2)"));
}

void MutationTests::testFileRename() {
    glancelot::MutationService service;
    QSignalSpy renamedSpy(&service, &glancelot::MutationService::pathRenamed);

    const QString parent = m_tempDir->path();
    const QString oldFile = QDir(parent).filePath(QStringLiteral("sample.txt"));
    {
        QFile f(oldFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("test content");
    }

    // Rename sample.txt -> renamed.txt
    QVariantMap res = service.renamePath(oldFile, QStringLiteral("renamed.txt"));
    QVERIFY(res.value(QStringLiteral("success")).toBool());
    const QString newFile = res.value(QStringLiteral("newPath")).toString();
    QVERIFY(QFileInfo::exists(newFile));
    QVERIFY(!QFileInfo::exists(oldFile));

    // Verify signal
    QCOMPARE(renamedSpy.count(), 1);
    const QList<QVariant> args = renamedSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QDir::cleanPath(oldFile));
    QCOMPARE(args.at(1).toString(), QDir::cleanPath(newFile));

    // Non-existent source failure
    res = service.renamePath(QDir(parent).filePath(QStringLiteral("ghost.txt")), QStringLiteral("test.txt"));
    QVERIFY(!res.value(QStringLiteral("success")).toBool());
    QVERIFY(res.value(QStringLiteral("error")).toString().contains(QStringLiteral("does not exist")));

    // Rename with invalid name failure
    res = service.renamePath(newFile, QStringLiteral("bad*name.txt"));
    QVERIFY(!res.value(QStringLiteral("success")).toBool());
}

void MutationTests::testCaseOnlyRename() {
    glancelot::MutationService service;
    QSignalSpy renamedSpy(&service, &glancelot::MutationService::pathRenamed);

    const QString parent = m_tempDir->path();
    const QString oldFile = QDir(parent).filePath(QStringLiteral("case_test.png"));
    {
        QFile f(oldFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("png data");
    }

    // Case-only rename: case_test.png -> Case_Test.png
    QVariantMap res = service.renamePath(oldFile, QStringLiteral("Case_Test.png"));
    QVERIFY(res.value(QStringLiteral("success")).toBool());
    const QString newFile = res.value(QStringLiteral("newPath")).toString();
    QVERIFY(QFileInfo::exists(newFile));

    // Verify content preserved
    QFile f(newFile);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("png data"));

    QCOMPARE(renamedSpy.count(), 1);
}

void MutationTests::testDirectoryRename() {
    glancelot::MutationService service;
    QSignalSpy renamedSpy(&service, &glancelot::MutationService::pathRenamed);

    const QString parent = m_tempDir->path();
    const QString subDir = QDir(parent).filePath(QStringLiteral("folder_a"));
    QVERIFY(QDir(parent).mkdir(QStringLiteral("folder_a")));

    // Put a child file in folder_a
    const QString childFile = QDir(subDir).filePath(QStringLiteral("child.jpg"));
    {
        QFile f(childFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("child");
    }

    // Rename folder_a -> folder_b
    QVariantMap res = service.renamePath(subDir, QStringLiteral("folder_b"));
    QVERIFY(res.value(QStringLiteral("success")).toBool());
    const QString newSubDir = res.value(QStringLiteral("newPath")).toString();
    QVERIFY(QDir(newSubDir).exists());
    QVERIFY(!QDir(subDir).exists());

    // Verify child file moved along with folder
    const QString newChildFile = QDir(newSubDir).filePath(QStringLiteral("child.jpg"));
    QVERIFY(QFileInfo::exists(newChildFile));

    QCOMPARE(renamedSpy.count(), 1);
}

void MutationTests::testCreateFolder() {
    glancelot::MutationService service;
    QSignalSpy createdSpy(&service, &glancelot::MutationService::folderCreated);

    const QString parent = m_tempDir->path();

    // Create folder "Vacation 2026"
    QVariantMap res = service.createFolder(parent, QStringLiteral("Vacation 2026"));
    QVERIFY(res.value(QStringLiteral("success")).toBool());
    const QString createdPath = res.value(QStringLiteral("path")).toString();
    QVERIFY(QDir(createdPath).exists());

    QCOMPARE(createdSpy.count(), 1);
    QCOMPARE(createdSpy.takeFirst().at(0).toString(), createdPath);

    // Creating folder with same name fails (collision)
    res = service.createFolder(parent, QStringLiteral("Vacation 2026"));
    QVERIFY(!res.value(QStringLiteral("success")).toBool());
    QVERIFY(res.value(QStringLiteral("error")).toString().contains(QStringLiteral("already exists")));

    // Creating folder in non-existent directory fails
    res = service.createFolder(QDir(parent).filePath(QStringLiteral("missing_parent")), QStringLiteral("sub"));
    QVERIFY(!res.value(QStringLiteral("success")).toBool());
    QVERIFY(res.value(QStringLiteral("error")).toString().contains(QStringLiteral("Parent directory does not exist")));

    // Creating folder with invalid characters fails
    res = service.createFolder(parent, QStringLiteral("invalid/name"));
    QVERIFY(!res.value(QStringLiteral("success")).toBool());
}

void MutationTests::testFolderHistoryReconciliation() {
    FolderHistory history;

    const QString dirA = QDir::cleanPath(QStringLiteral("/test/root/folderA"));
    const QString fileA = QDir::cleanPath(QStringLiteral("/test/root/folderA/image1.jpg"));
    const QString dirB = QDir::cleanPath(QStringLiteral("/test/root/folderB"));
    const QString fileB = QDir::cleanPath(QStringLiteral("/test/root/folderB/image2.jpg"));

    // Visit folderA and then folderB
    history.recordVisit(dirA, fileA, fileA, 10);
    history.recordVisit(dirB, fileB, fileB, 20);

    QCOMPARE(history.backCount(), 1);
    QCOMPARE(history.currentDirectory(), dirB);

    // Reconcile rename of folderA -> folderA_Renamed
    const QString dirARenamed = QDir::cleanPath(QStringLiteral("/test/root/folderA_Renamed"));
    history.reconcileRenamedPath(dirA, dirARenamed);

    // The back stack entry for folderA should now point to folderA_Renamed
    QCOMPARE(history.backDirectory(), dirARenamed);

    // Reconcile rename of currently open folderB -> folderB_Renamed
    const QString dirBRenamed = QDir::cleanPath(QStringLiteral("/test/root/folderB_Renamed"));
    history.reconcileRenamedPath(dirB, dirBRenamed);
    QCOMPARE(history.currentDirectory(), dirBRenamed);
}

void MutationTests::testViewerControllerReconciliation() {
    ViewerController controller;
    QVERIFY(controller.mutationService() != nullptr);

    QSignalSpy filesInvalidatedSpy(&controller, &ViewerController::filesInvalidated);
    QSignalSpy stateChangedSpy(&controller, &ViewerController::stateChanged);

    // Test reconcileCreatedFolder emits filesInvalidated
    controller.reconcileCreatedFolder(QStringLiteral("/test/new_folder"));
    QCOMPARE(filesInvalidatedSpy.count(), 1);

    // Test reconcileRenamedPath
    controller.reconcileRenamedPath(QStringLiteral("/old/path.jpg"), QStringLiteral("/new/path.jpg"));
    QCOMPARE(filesInvalidatedSpy.count(), 2);
}

QTEST_MAIN(MutationTests)
#include "mutation_tests.moc"

