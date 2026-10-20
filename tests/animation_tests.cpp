#include <QTest>
#include "glancelot/animation_controller.hpp"

class AnimationTests : public QObject {
    Q_OBJECT
private slots:
    void testInitialState();
};

void AnimationTests::testInitialState() {
    glancelot::AnimationController controller;
    QCOMPARE(controller.isActive(), false);
    QCOMPARE(controller.isAnimated(), false);
    QCOMPARE(controller.isPlaying(), false);
    QCOMPARE(controller.currentFrame(), 0);
    QCOMPARE(controller.frameCount(), 0);
    QCOMPARE(controller.speed(), 1.0);
    QCOMPARE(controller.isLoop(), true);
}

QTEST_MAIN(AnimationTests)
#include "animation_tests.moc"
