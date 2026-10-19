#include "image_canvas.hpp"
#include <QtTest>
#include <limits>

class CanvasTests final : public QObject {
    Q_OBJECT
    static QImage fixture(int width=1000,int height=500) {
        QImage result(width,height,QImage::Format_RGBA8888);
        result.fill(Qt::red);
        return result;
    }
    static bool close(QPointF a,QPointF b) { return QLineF(a,b).length()<0.00001; }
private slots:
    void fitModes() {
        ImageCanvas canvas;
        canvas.setSize({500,400}); canvas.setImage(fixture());
        QCOMPARE(canvas.fitMode(),QString("shrink")); QCOMPARE(canvas.zoom(),.5);
        canvas.fit("fit"); QCOMPARE(canvas.zoom(),.5);
        canvas.fit("fill"); QCOMPARE(canvas.zoom(),.8);
        canvas.fit("width"); QCOMPARE(canvas.zoom(),.5);
        canvas.fit("height"); QCOMPARE(canvas.zoom(),.8);
        canvas.fit("invalid"); QCOMPARE(canvas.fitMode(),QString("height"));
        canvas.setImage(fixture(100,50)); QCOMPARE(canvas.zoom(),1.0);
        canvas.fit("fit"); QCOMPARE(canvas.zoom(),5.0);
        canvas.fit("fill"); QCOMPARE(canvas.zoom(),8.0);
    }
    void portraitAndResize() {
        ImageCanvas canvas;
        canvas.setSize({500,400}); canvas.setImage(fixture(500,1000));
        QCOMPARE(canvas.zoom(),.4);
        canvas.setSize({1000,800}); QCOMPARE(canvas.zoom(),.8);
        canvas.actualSize(); QCOMPARE(canvas.zoom(),1.0);
        canvas.setSize({50,40}); QCOMPARE(canvas.zoom(),1.0);
    }
    void cursorZoomAndPan() {
        ImageCanvas canvas;
        canvas.setSize({500,400}); canvas.setImage(fixture());
        const QPointF anchor(137,91);
        const auto before=canvas.imagePointAt(anchor.x(),anchor.y());
        canvas.zoomAt(2,anchor.x(),anchor.y());
        QVERIFY(close(canvas.imagePointAt(anchor.x(),anchor.y()),before));
        QCOMPARE(canvas.zoom(),1.0);
        const auto center=canvas.imagePointAt(250,200);
        canvas.panBy(20,-30);
        QVERIFY(close(canvas.imagePointAt(250,200),center+QPointF(-20,30)));
        QVERIFY(canvas.fitMode().isEmpty());
    }
    // F037: presets about the centre, zoom lock across photos, and the navigator's visible rectangle and recentring.
    void presetsZoomLockAndNavigator() {
        ImageCanvas canvas;
        canvas.setSize({500,400}); canvas.setImage(fixture());
        const auto center=canvas.imagePointAt(250,200);
        canvas.zoomTo(2); QCOMPARE(canvas.zoom(),2.0); QVERIFY(canvas.fitMode().isEmpty());
        QVERIFY(close(canvas.imagePointAt(250,200),center)); // about the view centre
        // Without the lock a new photo fits again; with it the magnification stays, centred and upright.
        canvas.setImage(fixture(800,600)); QCOMPARE(canvas.fitMode(),QString("shrink")); QVERIFY(canvas.zoom()<=1.0);
        canvas.zoomTo(2); canvas.setZoomLock(true);
        canvas.setImage(fixture(900,300)); QCOMPARE(canvas.zoom(),2.0); QVERIFY(canvas.fitMode().isEmpty());
        QVERIFY(close(canvas.imagePointAt(250,200),QPointF(450,150)));
        canvas.fit("shrink"); canvas.setImage(fixture(700,300)); // a fitted view never becomes locked
        QCOMPARE(canvas.fitMode(),QString("shrink"));
        // Visible rectangle: the whole photo when fitted, a window onto it when zoomed; centerOn puts a point mid-view.
        canvas.setImage(fixture(1000,500)); canvas.setZoomLock(false);
        QCOMPARE(canvas.visibleImageRect(),QRectF(0,0,1000,500));
        canvas.zoomTo(2);
        const QRectF zoomed=canvas.visibleImageRect(); QCOMPARE(zoomed.width(),250.0); QCOMPARE(zoomed.height(),200.0);
        for (int turns=0; turns<4; ++turns) {
            canvas.centerOn(700,120);
            QVERIFY(close(canvas.imagePointAt(250,200),QPointF(700,120)));
            QVERIFY(canvas.visibleImageRect().contains(QPointF(700,120)));
            canvas.rotateClockwise(); canvas.zoomTo(2);
        }
    }
    void loupeRestoresExactly() {
        ImageCanvas canvas;
        canvas.setSize({500,400}); canvas.setImage(fixture());
        canvas.panBy(17,23);
        const auto before=canvas.imagePointAt(250,200);
        const auto originalFit=canvas.fitMode(); const auto originalZoom=canvas.zoom();
        canvas.beginLoupe(123,78); QCOMPARE(canvas.zoom(),1.0);
        canvas.beginLoupe(20,20); // Repeated hold does not overwrite the saved state.
        canvas.panBy(45,33);
        canvas.endLoupe(); canvas.endLoupe();
        QCOMPARE(canvas.zoom(),originalZoom); QCOMPARE(canvas.fitMode(),originalFit);
        QVERIFY(close(canvas.imagePointAt(250,200),before));
    }
    void imageChangeCancelsHold() {
        ImageCanvas canvas;
        canvas.setSize({500,400}); canvas.setImage(fixture());
        canvas.beginLoupe(20,20);
        canvas.setImage(fixture(100,50));
        canvas.endLoupe();
        QCOMPARE(canvas.zoom(),1.0); QCOMPARE(canvas.fitMode(),QString("shrink"));
        QVERIFY(close(canvas.imagePointAt(250,200),QPointF(50,25)));
    }
    void rotationAndInverseMapping() {
        ImageCanvas canvas;
        canvas.setSize({500,400}); canvas.setImage(fixture());
        canvas.rotateClockwise(); QCOMPARE(canvas.rotation(),90); QCOMPARE(canvas.zoom(),.4);
        QVERIFY(close(canvas.imagePointAt(250,200),QPointF(500,250)));
        QVERIFY(close(canvas.imagePointAt(290,200),QPointF(500,150)));
        const auto before=canvas.imagePointAt(155,89);
        canvas.zoomAt(3,155,89);
        QVERIFY(close(canvas.imagePointAt(155,89),before));
        canvas.rotateClockwise(); canvas.rotateClockwise(); canvas.rotateClockwise();
        QCOMPARE(canvas.rotation(),0);
    }
    void emptyAndInvalidInputs() {
        ImageCanvas canvas;
        canvas.setSize({500,400});
        canvas.zoomAt(2,50,50); canvas.panBy(20,20); canvas.beginLoupe(20,20); canvas.endLoupe();
        QCOMPARE(canvas.zoom(),1.0);
        canvas.setImage(fixture()); const auto original=canvas.zoom();
        canvas.zoomAt(0,0,0); canvas.zoomAt(-1,0,0);
        canvas.zoomAt(std::numeric_limits<double>::quiet_NaN(),0,0);
        canvas.zoomAt(2,std::numeric_limits<double>::infinity(),0);
        QCOMPARE(canvas.zoom(),original);
        canvas.zoomAt(1e20,250,200); QCOMPARE(canvas.zoom(),64.0);
        canvas.zoomAt(1e-20,250,200); QCOMPARE(canvas.zoom(),.001);
        canvas.setImage({}); canvas.endLoupe(); QVERIFY(canvas.image().isNull());
    }
};
QTEST_MAIN(CanvasTests)
#include "canvas_tests.moc"
