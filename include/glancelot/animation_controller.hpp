#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <memory>

namespace glancelot {

class AnimationController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(bool isAnimated READ isAnimated NOTIFY isAnimatedChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY frameCountChanged)
    Q_PROPERTY(qreal speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(bool loop READ isLoop WRITE setLoop NOTIFY loopChanged)
    Q_PROPERTY(QImage frameImage READ frameImage NOTIFY frameImageChanged)
    Q_PROPERTY(QString frameLabel READ frameLabel NOTIFY frameLabelChanged)
    Q_PROPERTY(int currentDelayMs READ currentDelayMs NOTIFY currentFrameChanged)

public:
    explicit AnimationController(QObject* parent = nullptr);
    ~AnimationController() override;

    bool isActive() const;
    bool isAnimated() const;
    bool isPlaying() const;
    int currentFrame() const;
    int frameCount() const;
    qreal speed() const;
    bool isLoop() const;
    QImage frameImage() const;
    QString frameLabel() const;
    int currentDelayMs() const;

public Q_SLOTS:
    void play();
    void pause();
    void togglePlayPause();
    void nextFrame();
    void previousFrame();
    void jumpToFrame(int frame);
    void setSpeed(qreal speedMultiplier);
    void setLoop(bool loop);
    void load(const QString& filePath, quint64 generation = 0);
    void clear();

Q_SIGNALS:
    void activeChanged();
    void isAnimatedChanged();
    void playingChanged();
    void currentFrameChanged();
    void frameCountChanged();
    void speedChanged();
    void loopChanged();
    void frameImageChanged();
    void frameLabelChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace glancelot
