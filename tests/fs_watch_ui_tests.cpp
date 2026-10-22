#include <QtTest>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QFontDatabase>
#include "glancelot/viewer_controller.hpp"
#include "glancelot/pane_layout.hpp"
#include "glancelot/theme_manager.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "glancelot/file_operation_service.hpp"
#include "file_grid_wiring.hpp"
#include "image_canvas.hpp"

class FSWatchUiTests : public QObject {
    Q_OBJECT
    QTemporaryDir config;
private slots:
    void initTestCase() {
        QVERIFY(config.isValid());qputenv("GLANCELOT_CONFIG_DIR",config.path().toUtf8());
        QQuickStyle::setStyle("Basic");
        for(const char* face:{"Geist-Light","Geist-Regular","Geist-Medium","Geist-SemiBold","GeistMono-Regular"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/geist/%1.ttf").arg(QLatin1String(face)));
        qmlRegisterType<ImageCanvas>("Glancelot",1,0,"ImageCanvas");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")),"Glancelot",1,0,"Theme");
    }
    void workspaceRefreshAndTextModalPrecedence() {
        QTemporaryDir media;QVERIFY(media.isValid());
        ViewerController viewer;PaneLayout layout;ThemeManager theme;
        FilmstripModel filmstrip;FolderTreeModel folders;DirectoryEntryModel grid;FileOperationService operations;
        QQmlApplicationEngine engine;
        auto imports=engine.importPathList();imports.removeAll(QCoreApplication::applicationDirPath());engine.setImportPathList(imports);
        for(const auto& item:QList<QPair<QString,QObject*>>{{"viewer",&viewer},{"paneLayout",&layout},{"theme",&theme},
                {"filmstrip",&filmstrip},{"folderTree",&folders},{"fileGrid",&grid},{"fileOperations",&operations}})
            engine.rootContext()->setContextProperty(item.first,item.second);
        file_grid_wiring::followViewer(viewer,grid);
        QObject::connect(&viewer,&ViewerController::stateChanged,&filmstrip,[&]{filmstrip.sync(viewer.snapshot());});
        QObject::connect(&viewer,&ViewerController::filesInvalidated,&filmstrip,[&]{filmstrip.invalidate();});
        file_grid_wiring::lockThumbnailEpoch(filmstrip,grid);
        engine.load(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        window->resize(1180,780);window->requestActivate();
        viewer.navigateDirectory(media.path());QTRY_VERIFY_WITH_TIMEOUT(!viewer.directoryPending(),10000);
        QSignalSpy invalidated(&viewer,&ViewerController::filesInvalidated);
        for(const auto& mode:{QString("preview"),QString("grid"),QString("combined")}) {
            QVERIFY(layout.setContentMode(mode));
            QTRY_COMPARE(window->property("gridMode").toBool(),mode=="grid");
            if(mode!="preview") {
                auto* view=window->findChild<QQuickItem*>("photoGridView");QVERIFY(view);view->forceActiveFocus();
            } else window->contentItem()->forceActiveFocus();
            QTRY_VERIFY(!window->property("editingText").toBool());
            const auto before=invalidated.size();QTest::keyClick(window,Qt::Key_F5);
            QTRY_VERIFY_WITH_TIMEOUT(invalidated.size()>before,5000);
        }
        QQmlComponent editorComponent(&engine);
        editorComponent.setData("import QtQuick\nTextInput { text: \"generated test input\"; width: 200; height: 32 }",QUrl());
        std::unique_ptr<QObject> editorObject(editorComponent.create());auto* editor=qobject_cast<QQuickItem*>(editorObject.get());QVERIFY(editor);
        editor->setParentItem(window->contentItem());editor->forceActiveFocus();QTRY_VERIFY(window->property("editingText").toBool());
        auto before=invalidated.size();QTest::keyClick(window,Qt::Key_F5);QTest::qWait(250);QCOMPARE(invalidated.size(),before);
        editorObject.reset();window->contentItem()->forceActiveFocus();
        viewer.trigger("settings");QTRY_VERIFY(window->property("modalOpen").toBool());
        before=invalidated.size();QTest::keyClick(window,Qt::Key_F5);QTest::qWait(250);QCOMPARE(invalidated.size(),before);
        auto* settings=window->findChild<QObject*>("settingsDialog");QVERIFY(settings);QVERIFY(QMetaObject::invokeMethod(settings,"close"));
        QTRY_VERIFY(!window->property("modalOpen").toBool());window->close();
    }
};
QTEST_MAIN(FSWatchUiTests)
#include "fs_watch_ui_tests.moc"
