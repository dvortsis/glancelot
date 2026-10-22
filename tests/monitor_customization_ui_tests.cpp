#include <QtTest>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include "glancelot/viewer_controller.hpp"
#include "glancelot/theme_manager.hpp"
#include "glancelot/pane_layout.hpp"
#include "glancelot/monitor_customization.hpp"
#include "glancelot/filmstrip_model.hpp"
#include "glancelot/folder_tree_model.hpp"
#include "glancelot/directory_entry_model.hpp"
#include "glancelot/file_operation_service.hpp"
#include "file_grid_wiring.hpp"
#include "image_canvas.hpp"

class MonitorCustomizationUiTests:public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> config;
    struct Harness {
        ViewerController viewer;ThemeManager theme;PaneLayout layout;FilmstripModel filmstrip;FolderTreeModel tree;
        DirectoryEntryModel grid;FileOperationService operations;
        glancelot::MonitorCustomization monitor{viewer.displayManager(),&layout};
        QQmlApplicationEngine engine;QQuickWindow* window{};
        Harness(){
            layout.setSlot(1);monitor.setSlot(1);
            engine.rootContext()->setContextProperty("viewer",&viewer);engine.rootContext()->setContextProperty("theme",&theme);
            engine.rootContext()->setContextProperty("paneLayout",&layout);engine.rootContext()->setContextProperty("monitorCustomization",&monitor);
            engine.rootContext()->setContextProperty("filmstrip",&filmstrip);engine.rootContext()->setContextProperty("folderTree",&tree);
            engine.rootContext()->setContextProperty("fileGrid",&grid);engine.rootContext()->setContextProperty("fileOperations",&operations);
            engine.load(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Main.qml")));
            if(!engine.rootObjects().isEmpty())window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            monitor.setControllerWindow(window);
        }
    };
private slots:
    void initTestCase(){QQuickStyle::setStyle("Basic");qmlRegisterType<ImageCanvas>("Glancelot",1,0,"ImageCanvas");qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(GLANCELOT_TEST_SOURCE_DIR "/ui/Theme.qml")),"Glancelot",1,0,"Theme");}
    void init(){config=std::make_unique<QTemporaryDir>();qputenv("GLANCELOT_CONFIG_DIR",config->path().toUtf8());}
    void cleanup(){qunsetenv("GLANCELOT_CONFIG_DIR");config.reset();}
    void everyRealPaneMigratesAndReturnsWithoutDuplication() {
        Harness h;QVERIFY(h.window);h.window->resize(1180,780);h.window->show();
        const auto display=h.viewer.displayManager()->displays().first().id;
        const QStringList panes{"tree","grid","preview","metadata"},objects{"folderTree","photoGrid","canvas","metadataHud"};
        for(int i=0;i<panes.size();++i){auto* item=h.window->findChild<QQuickItem*>(objects[i]);QVERIFY(item);auto* original=item;
            QVERIFY(h.monitor.detachPane(panes[i],display));QTRY_VERIFY(item->window()!=h.window);QVERIFY(item->window());QCOMPARE(h.window->findChild<QQuickItem*>(objects[i]),original);
            QVERIFY(h.monitor.redockPane(panes[i]));QTRY_COMPARE(item->window(),h.window);
        }
    }
    void registryRouteAndLockedPredicateRemainShared() {
        Harness h;QVERIFY(h.window);QVERIFY(QMetaObject::invokeMethod(h.window,"invoke",Q_ARG(QVariant,"monitor-settings")));
        auto* dialog=h.window->findChild<QObject*>("monitorSettings");QVERIFY(dialog);QTRY_VERIFY(dialog->property("visible").toBool());
        QMetaObject::invokeMethod(dialog,"close");
        QVERIFY(h.monitor.setPaneFrozen("grid",true));
        QVariant blocked;QVERIFY(QMetaObject::invokeMethod(h.window,"commandBlockedReason",Q_RETURN_ARG(QVariant,blocked),Q_ARG(QVariant,"pane-detach-grid")));QVERIFY(!blocked.toString().isEmpty());
        QVERIFY(h.viewer.setBinding("monitor-recover","Ctrl+Alt+9"));QVERIFY(!h.viewer.bindingFor("monitor-recover").isEmpty());
    }
};
QTEST_MAIN(MonitorCustomizationUiTests)
#include "monitor_customization_ui_tests.moc"
