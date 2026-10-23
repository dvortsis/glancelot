// COPY-SAFETY-01 review round 1: same-file aliases (B1), the replaced file keeping the original target's
// protection, and long paths and names. Uses only the public CopyMoveService API, the copy_safety fault hook's
// Read point and the operating system, so the same file also builds against the pre-fix service.
#include <QtTest>
#include <QTemporaryDir>
#include <memory>
#include "copy_safety.hpp"
#include "copy_safety_test_support.hpp"

using namespace cstest;
using Mode = CopyMoveService::TransferMode;

class CopySafetyTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // B1: the same file reached through two paths is never replaced by itself or deleted
    void aliasFolderSameFileIsRefused();
    void substDriveSameFileIsRefused();
    void substDriveFolderMoveIntoItselfKeepsEverything();
    void aliasFolderMoveIntoItselfKeepsEmptySubfolder();
    void hardLinkIsTheSameFile();
    void fileSymlinkToTargetIsRefused();

    // Lead decision: the replaced file keeps the original target's attributes, permissions and streams
    void replaceKeepsAttributesPermissionsAndStreams();
    void replaceKeepsRestrictivePermissions();

    // Long paths and names
    void overwriteLongPath();
    void overwriteLongFileName();

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QString root(const QString& name) const { return QDir(m_tempDir->path()).filePath(name); }
};

void CopySafetyTests::initTestCase() {
    m_tempDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tempDir->isValid());
}

namespace {

// A real folder with one file, and a second path (`alias`) to the same folder.
struct AliasFixture {
    QString realDir;
    QString aliasDir;
    QString file;      // through the real folder
    QString aliasFile; // the same file through the alias
    QByteArray hash;
};

bool makeAliasFixture(const QString& base, AliasFixture& fx) {
    fx.realDir = QDir(base).filePath(QStringLiteral("real"));
    fx.aliasDir = QDir(base).filePath(QStringLiteral("alias"));
    if (!QDir().mkpath(fx.realDir)) return false;
    fx.file = QDir(fx.realDir).filePath(QStringLiteral("file.bin"));
    fx.aliasFile = QDir(fx.aliasDir).filePath(QStringLiteral("file.bin"));
    if (!writeBytes(fx.file, makeData(600 * 1024 + 7, 11))) return false;
    fx.hash = sha256Of(fx.file);
    return makeFolderAlias(fx.realDir, fx.aliasDir);
}

} // namespace

void CopySafetyTests::aliasFolderSameFileIsRefused() {
    // Junction (Windows) or symbolic link (POSIX) to the target folder: Overwrite of "alias/file.bin" into the real
    // folder names one file twice. Before the fix Qt's canonical path did not resolve the junction.
    for (const Mode mode : {Mode::Copy, Mode::Move}) {
        AliasFixture fx;
        QVERIFY(makeAliasFixture(root(QStringLiteral("alias_folder_") + QLatin1String(modeName(mode))), fx));
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, fx.aliasFile, fx.realDir, mode);
        const QByteArray after = sha256Of(fx.file);
        const QStringList entries = entriesIn(fx.realDir);
        removeFolderAlias(fx.aliasDir);
        QVERIFY(outcome.finished);
        QCOMPARE(after, fx.hash);
        QCOMPARE(entries, QStringList{QStringLiteral("file.bin")});
        QCOMPARE(outcome.failed, 1);
        QCOMPARE(outcome.lastError, QStringLiteral("Source and destination are the same file."));
    }
}

void CopySafetyTests::substDriveSameFileIsRefused() {
#ifndef Q_OS_WIN
    QSKIP("subst drive letters exist only on Windows; the bind-mount equivalent needs root (see the handoff).");
#else
    // The reviewer's B1 reproduction: X: is subst'ed to the folder, so "X:/file.bin" and "<folder>/file.bin" are one
    // file on what look like two volumes. Before the fix the Move copied the file onto itself and then deleted it.
    for (const Mode mode : {Mode::Move, Mode::Copy}) {
        const QString dir = root(QStringLiteral("subst_file_") + QLatin1String(modeName(mode)));
        QVERIFY(QDir().mkpath(dir));
        const QString file = QDir(dir).filePath(QStringLiteral("file.bin"));
        QVERIFY(writeBytes(file, makeData(900 * 1024 + 3, 12)));
        const QByteArray hash = sha256Of(file);
        SubstDrive drive(dir);
        if (!drive.isValid()) QSKIP("subst is not available on this machine.");
        const QString aliasFile = drive.root() + QStringLiteral("file.bin");
        QCOMPARE(sha256Of(aliasFile), hash);

        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, aliasFile, dir, mode);
        QVERIFY(outcome.finished);
        QCOMPARE(sha256Of(file), hash);
        QCOMPARE(entriesIn(dir), QStringList{QStringLiteral("file.bin")});
        QCOMPARE(outcome.failed, 1);
        QCOMPARE(outcome.lastError, QStringLiteral("Source and destination are the same file."));
    }
#endif
}

void CopySafetyTests::substDriveFolderMoveIntoItselfKeepsEverything() {
#ifndef Q_OS_WIN
    QSKIP("subst drive letters exist only on Windows.");
#else
    // Overwrite Move of "X:/album" into its own parent: every file and the empty subfolder must survive.
    const QString parent = root(QStringLiteral("subst_folder"));
    const QString album = QDir(parent).filePath(QStringLiteral("album"));
    QVERIFY(QDir().mkpath(QDir(album).filePath(QStringLiteral("empty"))));
    const QString photo = QDir(album).filePath(QStringLiteral("photo.bin"));
    QVERIFY(writeBytes(photo, makeData(300 * 1024, 13)));
    const QByteArray hash = sha256Of(photo);
    SubstDrive drive(parent);
    if (!drive.isValid()) QSKIP("subst is not available on this machine.");

    CopyMoveService service;
    const BatchOutcome outcome = runTransfer(service, drive.root() + QStringLiteral("album"), parent, Mode::Move);
    QVERIFY(outcome.finished);
    QCOMPARE(sha256Of(photo), hash);
    QCOMPARE(entriesIn(album), (QStringList{QStringLiteral("empty"), QStringLiteral("photo.bin")}));
    QCOMPARE(outcome.failed, 1);
    QCOMPARE(outcome.lastError, QStringLiteral("Source and destination are the same folder."));
#endif
}

void CopySafetyTests::aliasFolderMoveIntoItselfKeepsEmptySubfolder() {
    // Junction or symlink "alias" -> "parent": Overwrite Move of "alias/album" into "parent". Before the fix the
    // files were refused or renamed onto themselves, but the empty subfolder was removed.
    const QString base = root(QStringLiteral("alias_folder_move"));
    const QString parent = QDir(base).filePath(QStringLiteral("parent"));
    const QString alias = QDir(base).filePath(QStringLiteral("alias"));
    const QString album = QDir(parent).filePath(QStringLiteral("album"));
    QVERIFY(QDir().mkpath(QDir(album).filePath(QStringLiteral("empty"))));
    const QString photo = QDir(album).filePath(QStringLiteral("photo.bin"));
    QVERIFY(writeBytes(photo, makeData(200 * 1024, 14)));
    const QByteArray hash = sha256Of(photo);
    QVERIFY(makeFolderAlias(parent, alias));

    CopyMoveService service;
    const BatchOutcome outcome = runTransfer(service, QDir(alias).filePath(QStringLiteral("album")), parent, Mode::Move);
    const QByteArray after = sha256Of(photo);
    const QStringList entries = entriesIn(album);
    removeFolderAlias(alias);
    QVERIFY(outcome.finished);
    QCOMPARE(after, hash);
    QCOMPARE(entries, (QStringList{QStringLiteral("empty"), QStringLiteral("photo.bin")}));
    QCOMPARE(outcome.failed, 1);
    QCOMPARE(outcome.lastError, QStringLiteral("Source and destination are the same folder."));
}

void CopySafetyTests::hardLinkIsTheSameFile() {
    // Two hard links are one file: replacing one with a copy of the other would silently split them.
    for (const Mode mode : {Mode::Copy, Mode::Move}) {
        const QString base = root(QStringLiteral("hardlink_") + QLatin1String(modeName(mode)));
        const QString dirA = QDir(base).filePath(QStringLiteral("a"));
        const QString dirB = QDir(base).filePath(QStringLiteral("b"));
        QVERIFY(QDir().mkpath(dirA));
        QVERIFY(QDir().mkpath(dirB));
        const QString fileA = QDir(dirA).filePath(QStringLiteral("shared.bin"));
        const QString fileB = QDir(dirB).filePath(QStringLiteral("shared.bin"));
        QVERIFY(writeBytes(fileA, makeData(400 * 1024, 15)));
        QVERIFY(makeHardLink(fileA, fileB));
        const QByteArray hash = sha256Of(fileA);

        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, fileA, dirB, mode);
        QVERIFY(outcome.finished);
        QCOMPARE(sha256Of(fileA), hash);
        QCOMPARE(sha256Of(fileB), hash);
        QCOMPARE(entriesIn(dirA), QStringList{QStringLiteral("shared.bin")});
        QCOMPARE(entriesIn(dirB), QStringList{QStringLiteral("shared.bin")});
        QCOMPARE(outcome.failed, 1);
        QCOMPARE(outcome.lastError, QStringLiteral("Source and destination are the same file."));
        // Still one file: a write through one name shows through the other.
        QVERIFY(writeBytes(fileA, QByteArray("changed")));
        QCOMPARE(sha256Of(fileB), sha256Of(fileA));
    }
}

void CopySafetyTests::fileSymlinkToTargetIsRefused() {
    // "a/photo.bin" is a symbolic link to "b/photo.bin". A same-volume Move would rename the link over the file it
    // points to, leaving a link to itself.
    const QString base = root(QStringLiteral("file_symlink"));
    const QString dirA = QDir(base).filePath(QStringLiteral("a"));
    const QString dirB = QDir(base).filePath(QStringLiteral("b"));
    QVERIFY(QDir().mkpath(dirA));
    QVERIFY(QDir().mkpath(dirB));
    const QString real = QDir(dirB).filePath(QStringLiteral("photo.bin"));
    const QString link = QDir(dirA).filePath(QStringLiteral("photo.bin"));
    QVERIFY(writeBytes(real, makeData(100 * 1024, 16)));
    const QByteArray hash = sha256Of(real);
    if (!makeFileSymlink(real, link)) QSKIP("This account cannot create symbolic links (Windows without Developer Mode).");
    for (const Mode mode : {Mode::Copy, Mode::Move}) {
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, link, dirB, mode);
        QVERIFY(outcome.finished);
        QCOMPARE(sha256Of(real), hash);
        QVERIFY(!QFileInfo(real).isSymLink());
        QCOMPARE(outcome.failed, 1);
        QCOMPARE(outcome.lastError, QStringLiteral("Source and destination are the same file."));
    }
}

void CopySafetyTests::replaceKeepsAttributesPermissionsAndStreams() {
#ifndef Q_OS_WIN
    // POSIX: the replaced file keeps the target's permission bits (here 0640 against the source's 0644).
    for (const Mode mode : {Mode::Copy, Mode::Move}) {
        const QString base = root(QStringLiteral("keep_mode_") + QLatin1String(modeName(mode)));
        const QString src = QDir(base).filePath(QStringLiteral("src/data.bin"));
        const QString dst = QDir(base).filePath(QStringLiteral("dst/data.bin"));
        QVERIFY(QDir().mkpath(QFileInfo(src).absolutePath()));
        QVERIFY(QDir().mkpath(QFileInfo(dst).absolutePath()));
        QVERIFY(writeBytes(src, makeData(300 * 1024, 17)));
        QVERIFY(writeBytes(dst, makeData(200 * 1024, 18)));
        QVERIFY(::chmod(QFile::encodeName(src).constData(), 0644) == 0);
        QVERIFY(::chmod(QFile::encodeName(dst).constData(), 0640) == 0);
        const QByteArray sourceHash = sha256Of(src);
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, src, QFileInfo(dst).absolutePath(), mode);
        QVERIFY(outcome.finished);
        QCOMPARE(outcome.succeeded, 1);
        QCOMPARE(sha256Of(dst), sourceHash);
        struct stat st;
        QVERIFY(::stat(QFile::encodeName(dst).constData(), &st) == 0);
        QCOMPARE(int(st.st_mode & 07777), 0640);
        QCOMPARE(entriesIn(QFileInfo(dst).absolutePath()), QStringList{QStringLiteral("data.bin")});
    }
#else
    // Hidden, system and not-indexed attributes, an explicit permission entry and an alternate data stream on the
    // target all survive the Overwrite; the content is the source's.
    for (const Mode mode : {Mode::Copy, Mode::Move}) {
        const QString base = root(QStringLiteral("keep_meta_") + QLatin1String(modeName(mode)));
        const QString srcDir = QDir(base).filePath(QStringLiteral("src"));
        const QString dstDir = QDir(base).filePath(QStringLiteral("dst"));
        QVERIFY(QDir().mkpath(srcDir));
        QVERIFY(QDir().mkpath(dstDir));
        const QString src = QDir(srcDir).filePath(QStringLiteral("data.bin"));
        const QString dst = QDir(dstDir).filePath(QStringLiteral("data.bin"));
        QVERIFY(writeBytes(src, makeData(300 * 1024, 17)));
        QVERIFY(writeBytes(dst, makeData(200 * 1024, 18)));
        const QByteArray sourceHash = sha256Of(src);
        QVERIFY(writeStream(dst, "Zone.Identifier", QByteArray("[ZoneTransfer]\r\nZoneId=3\r\n")));
        QVERIFY(grantLocalServiceRead(dst));
        constexpr DWORD kept = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
        QVERIFY(addAttributes(dst, kept));
        QVERIFY(aclStateOf(dst).localServiceExplicit);

        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, src, dstDir, mode);
        const DWORD attributes = attributesOf(dst);
        const AclState acl = aclStateOf(dst);
        const QByteArray stream = readStream(dst, "Zone.Identifier");
        const QStringList entries = entriesIn(dstDir);
        clearAttributes(dst);
        QVERIFY(outcome.finished);
        QVERIFY2(outcome.succeeded == 1, qPrintable(outcome.lastError));
        QCOMPARE(sha256Of(dst), sourceHash);
        QCOMPARE(entries, QStringList{QStringLiteral("data.bin")});
        QCOMPARE(QFile::exists(src), mode == Mode::Copy);
        QCOMPARE(attributes & kept, kept);
        QVERIFY(acl.read);
        QVERIFY(acl.localServiceExplicit);
        QCOMPARE(stream, QByteArray("[ZoneTransfer]\r\nZoneId=3\r\n"));
    }
#endif
}

void CopySafetyTests::replaceKeepsRestrictivePermissions() {
    // A target the user restricted to themselves stays restricted: the replaced file must not pick up the
    // folder's inherited (looser) permissions.
    for (const Mode mode : {Mode::Copy, Mode::Move}) {
        const QString base = root(QStringLiteral("keep_private_") + QLatin1String(modeName(mode)));
        const QString srcDir = QDir(base).filePath(QStringLiteral("src"));
        const QString dstDir = QDir(base).filePath(QStringLiteral("dst"));
        QVERIFY(QDir().mkpath(srcDir));
        QVERIFY(QDir().mkpath(dstDir));
        const QString src = QDir(srcDir).filePath(QStringLiteral("private.bin"));
        const QString dst = QDir(dstDir).filePath(QStringLiteral("private.bin"));
        QVERIFY(writeBytes(src, makeData(250 * 1024, 19)));
        QVERIFY(writeBytes(dst, makeData(150 * 1024, 20)));
        const QByteArray sourceHash = sha256Of(src);
#ifdef Q_OS_WIN
        QVERIFY(restrictToCurrentUser(dst));
        const AclState before = aclStateOf(dst);
        QVERIFY(before.protectedDacl);
        QCOMPARE(before.inheritedAces, 0);
#else
        QVERIFY(::chmod(QFile::encodeName(src).constData(), 0644) == 0);
        QVERIFY(::chmod(QFile::encodeName(dst).constData(), 0600) == 0);
#endif
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, src, dstDir, mode);
        QVERIFY(outcome.finished);
        QVERIFY2(outcome.succeeded == 1, qPrintable(outcome.lastError));
        QCOMPARE(sha256Of(dst), sourceHash);
        QCOMPARE(entriesIn(dstDir), QStringList{QStringLiteral("private.bin")});
#ifdef Q_OS_WIN
        const AclState after = aclStateOf(dst);
        QVERIFY(after.read);
        QVERIFY(after.protectedDacl);
        QCOMPARE(after.inheritedAces, 0);
        QCOMPARE(after.explicitAces, before.explicitAces);
#else
        struct stat st;
        QVERIFY(::stat(QFile::encodeName(dst).constData(), &st) == 0);
        QCOMPARE(int(st.st_mode & 07777), 0600);
#endif
    }
}

void CopySafetyTests::overwriteLongPath() {
    // A target path well over 260 characters: success for Copy and Move, and a failed copy keeps the target.
    QString deep = root(QStringLiteral("long_path"));
    for (int i = 0; i < 6; ++i) deep = QDir(deep).filePath(QString(50, QLatin1Char('a' + i)));
    const QString srcDir = root(QStringLiteral("long_path_src"));
    QVERIFY(QDir().mkpath(deep));
    QVERIFY(QDir().mkpath(srcDir));
    const QString target = QDir(deep).filePath(QStringLiteral("target.bin"));
    QVERIFY2(QDir::toNativeSeparators(target).size() > 300, qPrintable(QString::number(target.size())));

    for (const Mode mode : {Mode::Copy, Mode::Move}) {
        const QString src = QDir(srcDir).filePath(QStringLiteral("target.bin"));
        QVERIFY(writeBytes(src, makeData(500 * 1024, 21)));
        QVERIFY(writeBytes(target, makeData(300 * 1024, 22)));
        const QByteArray sourceHash = sha256Of(src);
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, src, deep, mode);
        QVERIFY(outcome.finished);
        QVERIFY2(outcome.succeeded == 1, qPrintable(outcome.lastError));
        QCOMPARE(sha256Of(target), sourceHash);
        QCOMPARE(QFile::exists(src), mode == Mode::Copy);
        QCOMPARE(entriesIn(deep), QStringList{QStringLiteral("target.bin")});
    }

    // Failure part-way through: the long-path target stays byte-identical and nothing is left beside it.
    const QString src = QDir(srcDir).filePath(QStringLiteral("target.bin"));
    QVERIFY(writeBytes(src, makeData(800 * 1024, 23)));
    const QByteArray targetHash = sha256Of(target);
    glancelot::copy_safety::setFaultHookForTesting([](glancelot::copy_safety::FaultPoint point, const QString&, qint64 bytes) {
        return point == glancelot::copy_safety::FaultPoint::Read && bytes >= 256 * 1024;
    });
    CopyMoveService service;
    const BatchOutcome outcome = runTransfer(service, src, deep, Mode::Copy);
    glancelot::copy_safety::setFaultHookForTesting({});
    QVERIFY(outcome.finished);
    QCOMPARE(outcome.failed, 1);
    QCOMPARE(sha256Of(target), targetHash);
    QVERIFY(QFile::exists(src));
    QCOMPARE(entriesIn(deep), QStringList{QStringLiteral("target.bin")});
}

void CopySafetyTests::overwriteLongFileName() {
    // A 240-character file name: the temporary name must not grow with it.
    const QString name = QString(236, QLatin1Char('n')) + QStringLiteral(".bin");
    for (const Mode mode : {Mode::Copy, Mode::Move}) {
        const QString base = root(QStringLiteral("long_name_") + QLatin1String(modeName(mode)));
        const QString srcDir = QDir(base).filePath(QStringLiteral("src"));
        const QString dstDir = QDir(base).filePath(QStringLiteral("dst"));
        QVERIFY(QDir().mkpath(srcDir));
        QVERIFY(QDir().mkpath(dstDir));
        const QString src = QDir(srcDir).filePath(name);
        const QString dst = QDir(dstDir).filePath(name);
        QVERIFY(writeBytes(src, makeData(200 * 1024, 24)));
        QVERIFY(writeBytes(dst, makeData(100 * 1024, 25)));
        const QByteArray sourceHash = sha256Of(src);
        CopyMoveService service;
        const BatchOutcome outcome = runTransfer(service, src, dstDir, mode);
        QVERIFY(outcome.finished);
        QVERIFY2(outcome.succeeded == 1, qPrintable(outcome.lastError));
        QCOMPARE(sha256Of(dst), sourceHash);
        QCOMPARE(entriesIn(dstDir), QStringList{name});
    }
}

QTEST_MAIN(CopySafetyTests)
#include "copy_safety_tests.moc"
