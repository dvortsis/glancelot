#pragma once
#include "canvas_texture_admission.hpp"
#include <memory>
#include <QQuickRhiItem>
#include <QImage>
#include <QColor>
#include <QVariantMap>
#include <QPointer>
#include <QQuickWindow>

// All viewport distances are logical pixels; zoom is physical pixels per source pixel.
class ImageCanvas : public QQuickRhiItem {
    Q_OBJECT
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(QString fitMode READ fitMode NOTIFY viewChanged)
    Q_PROPERTY(int rotation READ rotation NOTIFY viewChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY appearanceChanged)
    Q_PROPERTY(bool checkerboard READ checkerboard WRITE setCheckerboard NOTIFY appearanceChanged)
    Q_PROPERTY(bool nearest READ nearest WRITE setNearest NOTIFY appearanceChanged)
    Q_PROPERTY(QString rendererError READ rendererError NOTIFY rendererErrorChanged)
public:
    explicit ImageCanvas(QQuickItem* parent = nullptr);
    ~ImageCanvas() override;
    QImage image() const { return m_image; }
    void setImage(const QImage& image);
    qreal zoom() const { return m_zoom; }
    QString fitMode() const { return m_fitMode; }
    int rotation() const { return m_rotation; }
    QColor background() const { return m_background; }
    void setBackground(QColor color);
    bool checkerboard() const { return m_checkerboard; }
    void setCheckerboard(bool value);
    bool nearest() const { return m_nearest; }
    void setNearest(bool value);
    QString rendererError() const { return m_rendererError; }
    // Per-canvas GPU texture admission limit (default 256 MiB). Owners of several canvases lower it
    // so the aggregate stays bounded.
    qint64 textureBudget() const { return m_textureBudget; }
    qint64 admittedTextureBytes() const { return m_gpuAdmission->bytes(); }
    void setTextureBudget(qint64 bytes);
    // Internal presentation ownership: both canvases and all renderer incarnations share one cap.
    void shareTextureAdmissionWith(ImageCanvas* owner){m_gpuAdmission=owner->m_gpuAdmission;}
    void setAggregateTextureBudget(qint64 bytes){m_aggregateTextureBudget=bytes;update();}
    Q_INVOKABLE void fit(const QString& mode = QStringLiteral("shrink"));
    Q_INVOKABLE void actualSize();
    Q_INVOKABLE void zoomAt(qreal factor, qreal x, qreal y);
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    Q_INVOKABLE QVariantMap viewState() const;
    Q_INVOKABLE void setViewState(const QVariantMap& state);
    // Fixed 33-cube RGB atlas, shared by presentation canvases; null means identity.
    void setOutputColorLut(const QImage& atlas);
    // Continuous source coordinates, including points outside the image bounds.
    Q_INVOKABLE QPointF imagePointAt(qreal x, qreal y) const;
    Q_INVOKABLE void rotateClockwise();
    Q_INVOKABLE void beginLoupe(qreal x, qreal y);
    Q_INVOKABLE void endLoupe();
    // F037: zoom lock keeps a manual magnification across photos; zoomTo() applies a preset about the view centre
    // (1 = physical 1:1); the navigator reads visibleImageRect() and recentres with centerOn() (source pixels).
    Q_PROPERTY(bool zoomLock READ zoomLock WRITE setZoomLock NOTIFY zoomLockChanged)
    bool zoomLock() const { return m_zoomLock; }
    void setZoomLock(bool value);
    Q_INVOKABLE void zoomTo(qreal scale);
    Q_INVOKABLE QRectF visibleImageRect() const;
    Q_INVOKABLE void centerOn(qreal x, qreal y);
    // INPUT-02: exactly 1:1 around (x,y) while `sequence`'s key is held; releasing it, a deactivated window or an image
    // change restores the previous fit or zoom and pan exactly. A repeated begin keeps the first saved state.
    Q_INVOKABLE void beginHold(qreal x, qreal y, const QString& sequence);
    Q_PROPERTY(bool loupeActive READ loupeActive NOTIFY loupeChanged)
    bool loupeActive() const { return m_loupe; }
    Q_INVOKABLE void cycleBackground();
    void reportRendererError(const QString& message);
signals:
    void imageChanged();
    void viewChanged();
    void appearanceChanged();
    void rendererErrorChanged();
    void loupeChanged();
    void zoomLockChanged();
protected:
    QQuickRhiItemRenderer* createRenderer() override;
    void geometryChange(const QRectF&, const QRectF&) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void unwatchHold();
    int m_holdKey{};
    bool m_zoomLock{};
    QPointer<QQuickWindow> m_holdWindow;
    friend class ImageRenderer;
    qreal dpr() const;
    void refreshFit();
    QImage m_image;
    QImage m_outputColorLut;
    qreal m_zoom{1};
    qreal m_lastDpr{1};
    QPointF m_pan;
    QString m_fitMode{"shrink"};
    int m_rotation{};
    QColor m_background{"#191919"};
    bool m_checkerboard{}, m_nearest{}, m_loupe{};
    qreal m_savedZoom{1};
    QPointF m_savedPan;
    QString m_savedFit;
    QString m_rendererError;
    qint64 m_aggregateTextureBudget{256LL * 1024 * 1024};
    qint64 m_textureBudget{256LL * 1024 * 1024};
    std::shared_ptr<CanvasTextureAdmission> m_gpuAdmission=std::make_shared<CanvasTextureAdmission>();
};


