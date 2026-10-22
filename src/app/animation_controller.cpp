#include "glancelot/animation_controller.hpp"
#include <QImageReader>
#include <QThreadPool>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QFileInfo>
#include <QColorSpace>
#include <QPointer>
#include <algorithm>

namespace glancelot {

struct AnimationController::Private {
    AnimationController* q{nullptr};
    bool active{false};
    bool isAnimated{false};
    bool playing{false};
    int currentFrame{0};
    int frameCount{0};
    qreal speed{1.0};
    bool loop{true};
    QImage frameImage;
    QString frameLabel;
    int currentDelayMs{100};
    QString filePath;
    quint64 currentGeneration{0};
    QVector<int> frameDelays;
    QVector<QImage> frameCache;
    QTimer playbackTimer;
    mutable QMutex mutex;

    Private(AnimationController* parent) : q(parent) {
        playbackTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&playbackTimer, &QTimer::timeout, [this]() {
            onTimerTimeout();
        });
    }

    void updateFrameLabel() {
        if (!active || frameCount <= 0) {
            frameLabel.clear();
        } else if (isAnimated) {
            frameLabel = QStringLiteral("Frame %1 / %2").arg(currentFrame + 1).arg(frameCount);
        } else {
            frameLabel = QStringLiteral("Page %1 / %2").arg(currentFrame + 1).arg(frameCount);
        }
        emit q->frameLabelChanged();
    }

    void onTimerTimeout() {
        if (!playing || !active || frameCount <= 1) {
            return;
        }

        int nextIdx = currentFrame + 1;
        if (nextIdx >= frameCount) {
            if (loop) {
                nextIdx = 0;
            } else {
                q->pause();
                return;
            }
        }

        currentFrame = nextIdx;
        if (currentFrame < frameDelays.size() && frameDelays[currentFrame] > 0) {
            currentDelayMs = frameDelays[currentFrame];
        } else {
            currentDelayMs = 100;
        }

        {
            QMutexLocker locker(&mutex);
            if (currentFrame < frameCache.size() && !frameCache[currentFrame].isNull()) {
                frameImage = frameCache[currentFrame];
            }
        }

        updateFrameLabel();
        emit q->currentFrameChanged();
        emit q->frameImageChanged();

        if (playing) {
            const int interval = std::max(10, int(currentDelayMs / (speed > 0.01 ? speed : 1.0)));
            playbackTimer.start(interval);
        }
    }
};

AnimationController::AnimationController(QObject* parent)
    : QObject(parent), d(std::make_unique<Private>(this)) {}

AnimationController::~AnimationController() {
    d->currentGeneration++;
    d->playbackTimer.stop();
}

bool AnimationController::isActive() const {
    return d->active;
}

bool AnimationController::isAnimated() const {
    return d->isAnimated;
}

bool AnimationController::isPlaying() const {
    return d->playing;
}

int AnimationController::currentFrame() const {
    return d->currentFrame;
}

int AnimationController::frameCount() const {
    return d->frameCount;
}

qreal AnimationController::speed() const {
    return d->speed;
}

bool AnimationController::isLoop() const {
    return d->loop;
}

QImage AnimationController::frameImage() const {
    QMutexLocker locker(&d->mutex);
    return d->frameImage;
}

QString AnimationController::frameLabel() const {
    return d->frameLabel;
}

int AnimationController::currentDelayMs() const {
    return d->currentDelayMs;
}

void AnimationController::play() {
    if (!d->active || !d->isAnimated || d->frameCount <= 1 || d->playing) {
        return;
    }
    d->playing = true;
    const int interval = std::max(10, int(d->currentDelayMs / (d->speed > 0.01 ? d->speed : 1.0)));
    d->playbackTimer.start(interval);
    emit playingChanged();
}

void AnimationController::pause() {
    if (!d->playing) return;
    d->playing = false;
    d->playbackTimer.stop();
    emit playingChanged();
}

void AnimationController::togglePlayPause() {
    if (d->playing) {
        pause();
    } else {
        play();
    }
}

void AnimationController::nextFrame() {
    if (d->frameCount <= 1) return;
    pause();
    int next = d->currentFrame + 1;
    if (next >= d->frameCount) {
        next = d->loop ? 0 : d->frameCount - 1;
    }
    jumpToFrame(next);
}

void AnimationController::previousFrame() {
    if (d->frameCount <= 1) return;
    pause();
    int prev = d->currentFrame - 1;
    if (prev < 0) {
        prev = d->loop ? d->frameCount - 1 : 0;
    }
    jumpToFrame(prev);
}

void AnimationController::jumpToFrame(int frame) {
    if (!d->active || d->frameCount <= 0) return;
    frame = std::clamp(frame, 0, d->frameCount - 1);
    if (frame == d->currentFrame && !d->frameImage.isNull()) return;

    d->currentFrame = frame;
    if (frame < d->frameDelays.size() && d->frameDelays[frame] > 0) {
        d->currentDelayMs = d->frameDelays[frame];
    } else {
        d->currentDelayMs = 100;
    }

    {
        QMutexLocker locker(&d->mutex);
        if (frame < d->frameCache.size() && !d->frameCache[frame].isNull()) {
            d->frameImage = d->frameCache[frame];
        } else {
            // Lazy load frame on demand if not cached
            QImageReader reader(d->filePath);
            if (reader.jumpToImage(frame)) {
                QImage img = reader.read();
                if (!img.isNull()) {
                    if (frame < d->frameCache.size()) {
                        d->frameCache[frame] = img;
                    }
                    d->frameImage = img;
                }
            }
        }
    }

    d->updateFrameLabel();
    emit currentFrameChanged();
    emit frameImageChanged();
}

void AnimationController::setSpeed(qreal speedMultiplier) {
    const qreal clamped = std::clamp(speedMultiplier, 0.1, 4.0);
    if (qFuzzyCompare(clamped, d->speed)) return;
    d->speed = clamped;
    emit speedChanged();
    if (d->playing) {
        const int interval = std::max(10, int(d->currentDelayMs / d->speed));
        d->playbackTimer.start(interval);
    }
}

void AnimationController::setLoop(bool loop) {
    if (d->loop == loop) return;
    d->loop = loop;
    emit loopChanged();
}

void AnimationController::clear() {
    d->playbackTimer.stop();
    d->currentGeneration++;
    d->active = false;
    d->isAnimated = false;
    d->playing = false;
    d->currentFrame = 0;
    d->frameCount = 0;
    d->currentDelayMs = 100;
    d->filePath.clear();
    {
        QMutexLocker locker(&d->mutex);
        d->frameImage = QImage();
        d->frameCache.clear();
        d->frameDelays.clear();
    }
    d->updateFrameLabel();
    emit activeChanged();
    emit isAnimatedChanged();
    emit playingChanged();
    emit currentFrameChanged();
    emit frameCountChanged();
    emit frameImageChanged();
}

void AnimationController::load(const QString& filePath, quint64 generation) {
    d->playbackTimer.stop();
    if (generation == 0) {
        d->currentGeneration++;
        generation = d->currentGeneration;
    } else {
        d->currentGeneration = generation;
    }

    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        clear();
        emit loaded(d->currentGeneration);
        return;
    }

    d->filePath = filePath;
    const quint64 token = generation;
    QPointer<AnimationController> self(this);

    QThreadPool::globalInstance()->start([self, filePath, token]() {
        if (!self) return;

        QImageReader reader(filePath);
        reader.setDecideFormatFromContent(true);
        const QString fmt = QString::fromLatin1(reader.format()).toLower();
        const int count = reader.imageCount();

        if (count <= 1) {
            QMetaObject::invokeMethod(self.data(), [self, token]() {
                if (!self || token != self->d->currentGeneration) return;
                self->clear();
                emit self->loaded(token);
            }, Qt::QueuedConnection);
            return;
        }

        const bool isTiff = (fmt == "tiff" || fmt == "tif");
        const bool animated = !isTiff && (reader.supportsAnimation() || fmt == "gif" || fmt == "webp" || fmt == "png");

        // Pre-decode initial frames (up to 64 frames for bounded memory) and collect exact delays
        const int predecodeCount = std::min(count, 64);
        QVector<int> delays;
        delays.reserve(count);
        QVector<QImage> cache(count);

        for (int i = 0; i < count; ++i) {
            reader.jumpToImage(i);
            int delay = reader.nextImageDelay();
            if (i < predecodeCount) {
                QImage frame = reader.read();
                if (!frame.isNull()) {
                    if (!frame.colorSpace().isValid()) frame.setColorSpace(QColorSpace::SRgb);
                    cache[i] = frame;
                }
                const int dAfter = reader.nextImageDelay();
                if (dAfter > 0) delay = dAfter;
            }
            if (delay <= 0) delay = 100;
            delays.append(delay);
        }

        QMetaObject::invokeMethod(self.data(), [self, token, count, animated, delays = std::move(delays), cache = std::move(cache)]() mutable {
            if (!self || token != self->d->currentGeneration) return;

            self->d->active = true;
            self->d->isAnimated = animated;
            self->d->frameCount = count;
            self->d->currentFrame = 0;
            self->d->currentDelayMs = !delays.isEmpty() ? delays[0] : 100;
            self->d->frameDelays = std::move(delays);

            {
                QMutexLocker locker(&self->d->mutex);
                self->d->frameCache = std::move(cache);
                self->d->frameImage = !self->d->frameCache.isEmpty() ? self->d->frameCache[0] : QImage();
            }

            self->d->updateFrameLabel();
            emit self->activeChanged();
            emit self->isAnimatedChanged();
            emit self->frameCountChanged();
            emit self->currentFrameChanged();
            emit self->frameImageChanged();
            emit self->loaded(token);

            // Auto-play animated GIFs / WebPs by default
            if (self->d->isAnimated) {
                self->play();
            }
        }, Qt::QueuedConnection);
    });
}

} // namespace glancelot
