#include "startup_window_geometry.hpp"
#include <QtTest>
#include <array>
#include <limits>
using namespace glancelot::startup;

namespace {
bool positiveIntersection(const QRect& a,const QRect& b) {
    return std::max(qint64(a.left()),qint64(b.left())) < std::min(qint64(a.right())+1,qint64(b.right())+1)
        && std::max(qint64(a.top()),qint64(b.top())) < std::min(qint64(a.bottom())+1,qint64(b.bottom())+1);
}
}
class StartupWindowGeometryTests : public QObject {
    Q_OBJECT
private slots:
    void placement_data() {
        QTest::addColumn<QRect>("current"); QTest::addColumn<QRect>("screen");
        QTest::addColumn<QSize>("minimum"); QTest::addColumn<QRect>("expected");
        QTest::addColumn<bool>("recovered");
        QTest::newRow("real-failure-zero-margin") << QRect(0,1600,2360,1560) << QRect(0,0,2560,1440) << QSize(540,360) << QRect(100,0,2360,1440) << true;
        QTest::newRow("real-failure-decorated") << QRect(-8,1569,2376,1599) << QRect(0,0,2560,1440) << QSize(556,399) << QRect(92,0,2376,1440) << true;
        QTest::newRow("partially-visible") << QRect(-90,-90,100,100) << QRect(0,0,1000,800) << QSize(540,360) << QRect(-90,-90,100,100) << false;
        QTest::newRow("one-pixel-visible") << QRect(-99,50,100,100) << QRect(0,0,1000,800) << QSize(540,360) << QRect(-99,50,100,100) << false;
        QTest::newRow("touch-left") << QRect(-100,50,100,100) << QRect(0,0,1000,800) << QSize(540,360) << QRect(230,220,540,360) << true;
        QTest::newRow("touch-bottom") << QRect(0,800,600,400) << QRect(0,0,1000,800) << QSize(540,360) << QRect(200,200,600,400) << true;
        QTest::newRow("negative-origin") << QRect(5000,5000,600,400) << QRect(-1600,-900,1600,900) << QSize(540,360) << QRect(-1100,-650,600,400) << true;
        QTest::newRow("small-work-area") << QRect(5000,5000,1000,900) << QRect(-200,100,200,100) << QSize(540,360) << QRect(-200,100,540,360) << true;
        QTest::newRow("minimum-sized") << QRect(5000,5000,100,100) << QRect(0,0,1000,800) << QSize(540,360) << QRect(230,220,540,360) << true;
        QTest::newRow("invalid-current") << QRect(20,30,0,500) << QRect(0,0,1000,800) << QSize(540,360) << QRect(230,220,540,360) << true;
        QTest::newRow("invalid-minimum") << QRect() << QRect(0,0,1000,800) << QSize(-1,0) << QRect(499,399,1,1) << true;
        const auto hi = std::numeric_limits<int>::max(), lo = std::numeric_limits<int>::min();
        QTest::newRow("integer-upper-edge") << QRect(0,0,600,400) << QRect(QPoint(hi-999,hi-799),QPoint(hi,hi)) << QSize(540,360) << QRect(hi-799,hi-599,600,400) << true;
        QTest::newRow("integer-lower-edge") << QRect(0,0,600,400) << QRect(lo,lo,1000,800) << QSize(540,360) << QRect(lo+200,lo+200,600,400) << true;
        QTest::newRow("integer-minimum-larger-than-area") << QRect() << QRect(QPoint(hi-9,hi-9),QPoint(hi,hi)) << QSize(hi,hi) << QRect(QPoint(1,1),QPoint(hi,hi)) << true;
    }
    void placement() {
        QFETCH(QRect,current); QFETCH(QRect,screen); QFETCH(QSize,minimum); QFETCH(QRect,expected); QFETCH(bool,recovered);
        const std::array screens{screen}; const auto result = recoverStartupWindowGeometry(current,screens,0,minimum);
        QCOMPARE(result.geometry,expected);
        QCOMPARE(result.status,recovered ? StartupPlacementStatus::Recovered : StartupPlacementStatus::Unchanged);
        QVERIFY(positiveIntersection(result.geometry,screen));
        if (recovered) { QVERIFY(result.geometry.width() >= std::max(1,minimum.width())); QVERIFY(result.geometry.height() >= std::max(1,minimum.height())); }
    }
    void topologyAndPreference() {
        const std::array screens{QRect(0,0,2560,1440),QRect(0,-1600,2560,1600)};
        const auto result = recoverStartupWindowGeometry(QRect(0,1600,2360,1560),screens,1,QSize(540,360));
        QCOMPARE(result.status,StartupPlacementStatus::Recovered); QCOMPARE(result.geometry,QRect(100,-1580,2360,1560));
        const auto second = recoverStartupWindowGeometry(QRect(50,-1500,640,480),screens,0,QSize(540,360));
        QCOMPARE(second.status,StartupPlacementStatus::Unchanged); QCOMPARE(second.geometry,QRect(50,-1500,640,480));
        const std::array gaps{QRect(-2000,0,1000,800),QRect(1000,0,1000,800)};
        const QRect between(-100,100,200,400);
        QCOMPARE(recoverStartupWindowGeometry(between,gaps,1,QSize(540,360)).geometry,QRect(1230,200,540,400));
        QCOMPARE(recoverStartupWindowGeometry(between,gaps,99,QSize(540,360)).geometry,QRect(-1770,200,540,400));
        const std::array invalidPreferred{QRect(),gaps[1],gaps[0]};
        QCOMPARE(recoverStartupWindowGeometry(between,invalidPreferred,0,QSize(540,360)).geometry,QRect(1230,200,540,400));
    }
    void unavailableOrExcessiveScreens() {
        const QRect current(9999,9999,640,480); const QSize minimum(540,360);
        const std::array invalid{QRect(),QRect(0,0,-10,20),QRect(0,0,20,0)};
        for (auto screens : {std::span<const QRect>{},std::span<const QRect>{invalid}}) {
            const auto result = recoverStartupWindowGeometry(current,screens,0,minimum);
            QCOMPARE(result.status,StartupPlacementStatus::NoUsableScreen); QCOMPARE(result.geometry,current);
        }
        std::array<QRect,maximumStartupScreens+1> tooMany{};
        tooMany[0]=QRect(0,0,20000,20000);
        const auto result = recoverStartupWindowGeometry(current,tooMany,0,minimum);
        QCOMPARE(result.status,StartupPlacementStatus::TooManyScreens); QCOMPARE(result.geometry,current);
        const auto invalidCurrent = recoverStartupWindowGeometry(QRect{},tooMany,0,QSize{});
        QCOMPARE(invalidCurrent.status,StartupPlacementStatus::TooManyScreens); QCOMPARE(invalidCurrent.geometry,QRect{});
        const std::span<const QRect> atLimit(tooMany.data(),maximumStartupScreens);
        QCOMPARE(recoverStartupWindowGeometry(current,atLimit,0,minimum).status,StartupPlacementStatus::Unchanged);
    }
};
QTEST_GUILESS_MAIN(StartupWindowGeometryTests)
#include "startup_window_geometry_tests.moc"
