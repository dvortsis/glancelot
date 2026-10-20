#pragma once
#include <QQuickRhiItem>
#include <QImage>
#include <QColor>

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
    void setTextureBudget(qint64 bytes);
    Q_INVOKABLE void fit(const QString& mode = QStringLiteral("shrink"));
    Q_INVOKABLE void actualSize();
    Q_INVOKABLE void zoomAt(qreal factor, qreal x, qreal y);
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    // Continuous source coordinates, including points outside the image bounds.
    Q_INVOKABLE QPointF imagePointAt(qreal x, qreal y) const;
    Q_INVOKABLE void rotateClockwise();
    Q_INVOKABLE void beginLoupe(qreal x, qreal y);
    Q_INVOKABLE void endLoupe();
    Q_INVOKABLE void cycleBackground();
    void reportRendererError(const QString& message);
signals:
    void imageChanged();
    void viewChanged();
    void appearanceChanged();
    void rendererErrorChanged();
protected:
    QQuickRhiItemRenderer* createRenderer() override;
    void geometryChange(const QRectF&, const QRectF&) override;
private:
    friend class ImageRenderer;
    qreal dpr() const;
    void refreshFit();
    QImage m_image;
    qreal m_zoom{1};
    QPointF m_pan;
    QString m_fitMode{"shrink"};
    int m_rotation{};
    QColor m_background{"#191919"};
    bool m_checkerboard{}, m_nearest{}, m_loupe{};
    qreal m_savedZoom{1};
    QPointF m_savedPan;
    QString m_savedFit;
    QString m_rendererError;
    qint64 m_textureBudget{256LL * 1024 * 1024};
};


