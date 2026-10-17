#include "glancelot/viewer_controller.hpp"
#include "glancelot/navigation.hpp"
#include <functional>
#include <QColorSpace>
#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QImageWriter>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QString writePng(const QString& directory, const QString& name, const QColor& color,
                 const QSize size = QSize(17, 11)) {
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(color);
    image.setColorSpace(QColorSpace::SRgb);
    const auto path = directory + "/" + name;
    return image.save(path, "PNG") ? path : QString{};
}
QString writeOrientedJpeg(const QString& directory) {
    QImage image(30, 20, QImage::Format_RGB32);
    image.fill(Qt::red);
    for (int y=0; y<20; ++y)
        for (int x=15; x<30; ++x) image.setPixelColor(x,y,Qt::blue);
    image.setColorSpace(QColorSpace::SRgb);
    const auto base = directory + "/orientation-source.jpg";
    if (!image.save(base,"JPEG",100)) return {};
    QFile input(base);
    if (!input.open(QIODevice::ReadOnly)) return {};
    const auto bytes=input.readAll();
    // EXIF orientation 6 means rotate stored pixels 90 degrees clockwise.
    const auto exif=QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");
    QByteArray segment=QByteArray::fromHex("ffe1");
    const auto length=exif.size()+2;
    segment.append(char(length>>8)); segment.append(char(length&255)); segment+=exif;
    const auto path=directory+"/oriented.jpg";
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return {};
    output.write(bytes.first(2)); output.write(segment); output.write(bytes.sliced(2));
    output.close(); input.close(); QFile::remove(base);
    return path;
}
}

class AcceptanceTests final : public QObject {
    Q_OBJECT
private slots:
    void generatedTreeReverseOracle();
    void malformedInputTerminates();
    void pngPreservesDimensionsAlphaAndColor();
    void jpegOrientationApplied();
    void newestOpenWins();
    void duplicateBindingRejectedAndSettingsPersist();
    void stopAndLoopBoundaries();
    void boundaryChangeWhileScanPending();
};

void AcceptanceTests::generatedTreeReverseOracle() {
    QTemporaryDir tree; QVERIFY(tree.isValid());
    QStringList expected;
    std::function<void(const QString&,int)> enumerate=[&](const QString& directory,int depth) {
        expected << directory+"/n2.png" << directory+"/n10.png";
        if (depth==0) return;
        // Numeric names intentionally differ from lexical ordering.
        for (const auto& child : {"d2","d10","d30"}) enumerate(directory+"/"+child,depth-1);
    };
    enumerate(tree.path(),3);
    // Filesystem insertion order must not determine traversal order.
    for (auto it=expected.crbegin(); it!=expected.crend(); ++it) {
        QVERIFY(QDir().mkpath(QFileInfo(*it).absolutePath()));
        QFile file(*it); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture");
    }
    glancelot::Navigator navigator;
    std::error_code error;
    QVERIFY(navigator.open(std::filesystem::path(expected.front().toStdWString()),error));
    for (int index=1; index<expected.size(); ++index) {
        const auto next=navigator.step(1); QVERIFY2(next.has_value(),qPrintable(QString("ended at %1").arg(index)));
        QCOMPARE(QString::fromStdWString(next->wstring()),QDir::toNativeSeparators(expected.at(index)));
    }
    QVERIFY(!navigator.step(1));
    for (int index=expected.size()-2; index>=0; --index) {
        const auto previous=navigator.step(-1); QVERIFY(previous.has_value());
        QCOMPARE(QString::fromStdWString(previous->wstring()),QDir::toNativeSeparators(expected.at(index)));
    }
    QVERIFY(!navigator.step(-1));
}
void AcceptanceTests::malformedInputTerminates() {
    QTemporaryDir files; QVERIFY(files.isValid());
    QFile broken(files.filePath("broken.jpg"));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write(QByteArray::fromHex("ffd8ffe000104a464946000102") + QByteArray("invalid")); broken.close();
    ViewerController controller;
    QSignalSpy changed(&controller,&ViewerController::stateChanged);
    controller.open(broken.fileName());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && changed.count()>0,10000);
    QVERIFY(controller.image().isNull());
    QVERIFY2(!controller.status().isEmpty(),"Decode failure needs an actionable status");
    const auto good=writePng(files.path(),"recovery.png",Qt::green);
    QVERIFY(!good.isEmpty()); controller.open(good);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && !controller.image().isNull(),10000);
    QCOMPARE(controller.image().pixelColor(0,0),QColor(Qt::green));
}

void AcceptanceTests::pngPreservesDimensionsAlphaAndColor() {
    QTemporaryDir files; QVERIFY(files.isValid());
    const QColor expected(51,102,153,128);
    const auto path=writePng(files.path(),"known-color.png",expected);
    QVERIFY(!path.isEmpty()); ViewerController controller; controller.open(path);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && !controller.image().isNull(),10000);
    QCOMPARE(controller.imageWidth(),17); QCOMPARE(controller.imageHeight(),11);
    const auto actual=controller.image().pixelColor(8,5);
    QVERIFY(qAbs(actual.red()-expected.red())<=1);
    QVERIFY(qAbs(actual.green()-expected.green())<=1);
    QVERIFY(qAbs(actual.blue()-expected.blue())<=1);
    QCOMPARE(actual.alpha(),expected.alpha());
    QVERIFY(controller.image().colorSpace().isValid());
}

void AcceptanceTests::jpegOrientationApplied() {
    QTemporaryDir files; QVERIFY(files.isValid()); const auto path=writeOrientedJpeg(files.path());
    QVERIFY(!path.isEmpty()); ViewerController controller; controller.open(path);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && !controller.image().isNull(),10000);
    QCOMPARE(controller.imageWidth(),20); QCOMPARE(controller.imageHeight(),30);
    const auto top=controller.image().pixelColor(10,4);
    const auto bottom=controller.image().pixelColor(10,25);
    QVERIFY(top.red()>230 && top.blue()<25);
    QVERIFY(bottom.blue()>230 && bottom.red()<25);
}

void AcceptanceTests::newestOpenWins() {
    QTemporaryDir files; QVERIFY(files.isValid());
    const auto slow=writePng(files.path(),"large.png",Qt::red,QSize(4096,4096));
    const auto final=writePng(files.path(),"final.png",Qt::blue,QSize(7,5));
    QVERIFY(!slow.isEmpty() && !final.isEmpty());
    ViewerController controller;
    controller.open(slow); controller.open(final);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && controller.image().size()==QSize(7,5),15000);
    QCOMPARE(controller.image().pixelColor(0,0),QColor(Qt::blue));
    // Exercise event delivery after a superseded decode could have completed.
    QTest::qWait(500);
    QCOMPARE(controller.image().size(),QSize(7,5));
    QCOMPARE(QFileInfo(controller.filePath()).canonicalFilePath(),QFileInfo(final).canonicalFilePath());
}

void AcceptanceTests::duplicateBindingRejectedAndSettingsPersist() {
    QString first,second;
    {
        ViewerController controller; controller.resetBindings();
        for (const auto& item : controller.commands()) {
            const auto id=item.toMap().value("id").toString();
            if (id.isEmpty()) continue;
            if (first.isEmpty()) first=id; else { second=id; break; }
        }
        QVERIFY2(!first.isEmpty() && !second.isEmpty(),"Command registry exposes stable IDs");
        QVERIFY(controller.setBinding(first,"Ctrl+Alt+F11"));
        QVERIFY(!controller.setBinding(second,"Ctrl+Alt+F11"));
        QCOMPARE(controller.bindingFor(first),QString("Ctrl+Alt+F11"));
        controller.setBoundaryMode("stop"); controller.setWheelMode("zoom");
    }
    ViewerController restored;
    QCOMPARE(restored.bindingFor(first),QString("Ctrl+Alt+F11"));
    QCOMPARE(restored.boundaryMode(),QString("stop"));
    QCOMPARE(restored.wheelMode(),QString("zoom"));
    restored.resetBindings(); restored.setBoundaryMode("continue"); restored.setWheelMode("navigate");
}

void AcceptanceTests::stopAndLoopBoundaries() {
    QTemporaryDir files; QVERIFY(files.isValid());
    const auto a=writePng(files.path(),"image2.png",Qt::red);
    const auto b=writePng(files.path(),"image10.png",Qt::blue);
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    ViewerController controller; controller.setBoundaryMode("stop"); controller.open(a);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && !controller.image().isNull(),10000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.fileCount(),2,10000);
    controller.previous(); QTest::qWait(100);
    QCOMPARE(QFileInfo(controller.filePath()).fileName(),QString("image2.png"));
    controller.next();
    QTRY_COMPARE_WITH_TIMEOUT(QFileInfo(controller.filePath()).fileName(),QString("image10.png"),10000);
    controller.next(); QTest::qWait(100);
    QCOMPARE(QFileInfo(controller.filePath()).fileName(),QString("image10.png"));
    controller.setBoundaryMode("loop"); controller.next();
    QTRY_COMPARE_WITH_TIMEOUT(QFileInfo(controller.filePath()).fileName(),QString("image2.png"),10000);
    controller.previous();
    QTRY_COMPARE_WITH_TIMEOUT(QFileInfo(controller.filePath()).fileName(),QString("image10.png"),10000);
}

void AcceptanceTests::boundaryChangeWhileScanPending() {
    QTemporaryDir files; QVERIFY(files.isValid());
    QVERIFY(QDir().mkpath(files.filePath("child")));
    const auto root=writePng(files.path(),"root.png",Qt::red);
    const auto child=writePng(files.filePath("child"),"child.png",Qt::blue);
    QVERIFY(!root.isEmpty() && !child.isEmpty());
    ViewerController controller; controller.setBoundaryMode("continue");
    controller.open(root);
    // No event-loop turn has published the scan result. Changing the preference
    // now must win over the preference captured by the pending scan job.
    controller.setBoundaryMode("stop");
    QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && controller.fileCount()==1,10000);
    QSignalSpy stateChanged(&controller,&ViewerController::stateChanged);
    controller.next();
    QTRY_VERIFY_WITH_TIMEOUT(stateChanged.count()>0 && !controller.loading(),10000);
    QCOMPARE(QFileInfo(controller.filePath()).canonicalFilePath(),QFileInfo(root).canonicalFilePath());
    QCOMPARE(controller.boundaryMode(),QString("stop"));
    controller.setBoundaryMode("continue");
}
int main(int argc,char** argv) {
    QTemporaryDir preferences;
    qputenv("GLANCELOT_CONFIG_DIR",preferences.path().toUtf8());
    qputenv("APPDATA",preferences.path().toUtf8());
    qputenv("LOCALAPPDATA",preferences.path().toUtf8());
    qputenv("XDG_CONFIG_HOME",preferences.path().toUtf8());
    QGuiApplication application(argc,argv);
    QCoreApplication::setOrganizationName("GlancelotAcceptance");
    QCoreApplication::setApplicationName("GlancelotAcceptance");
    AcceptanceTests tests;
    return QTest::qExec(&tests,argc,argv);
}
#include "acceptance_tests.moc"




