    void reviewerHiddenCanvasMustReleaseShedImage() {
        const qint64 saved = glancelot::DisplayManager::imageBudgetBytes();
        struct Reset { qint64 n; ~Reset() { glancelot::DisplayManager::setImageBudgetBytes(n); } } reset{saved};
        glancelot::DisplayManager dm;
        QVERIFY(dm.openPresentation());
        dm.updateImage("a", createTestImage(40, 40, Qt::red), 1);
        dm.setComparisonImage("b", createTestImage(40, 40, Qt::blue), 2);
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QVERIFY(pw);
        dm.setMode(glancelot::PresentationMode::Mirror);
        const qint64 one = 40 * 40 * 4;
        glancelot::DisplayManager::setImageBudgetBytes(one);
        dm.updateImage("c", createTestImage(40, 40, Qt::green), 3);
        QCOMPARE(dm.retainedImageBytes(), one);
        dm.releaseCachedImages();
        qInfo() << "reported bytes" << dm.retainedImageBytes() << "hidden secondary bytes" << pw->secondaryCanvas()->image().sizeInBytes();
        QVERIFY2(pw->secondaryCanvas()->image().isNull(), "Shed/low-memory-released secondary pixels are retained by the hidden canvas outside accounting");
    }

    void reviewerOldComparisonCannotOverrideNewCompareCommand() {
        glancelot::DisplayManager dm;
        QVERIFY(dm.openPresentation());
        dm.updateImage("a", createTestImage(64, 64, Qt::red), 1);
        dm.updateImage("b", createTestImage(80, 80, Qt::green), 2);
        dm.setComparisonImage("new", createTestImage(40, 40, Qt::blue), 20);
        dm.compareWithPinnedOrPrevious();
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QCOMPARE(pw->secondaryCanvas()->image().size(), QSize(64, 64));
        dm.setComparisonImage("old", createTestImage(30, 30, Qt::yellow), 7);
        QCOMPARE(pw->secondaryCanvas()->image().size(), QSize(64, 64));
    }

    void reviewerInvalidFitMustNotDestroyRememberedFit() {
        glancelot::DisplayManager dm;
        QVERIFY(dm.openPresentation());
        dm.setOutputFitMode("fill");
        dm.setOutputFitMode("bogus");
        auto* pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QCOMPARE(pw->fitMode(), QStringLiteral("fill"));
        dm.closePresentation();
        QVERIFY(dm.openPresentation());
        pw = qobject_cast<glancelot::PresentationWindow*>(dm.presentationWindow());
        QCOMPARE(pw->fitMode(), QStringLiteral("fill"));
    }

