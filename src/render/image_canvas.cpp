#include "image_canvas.hpp"
#include <QQuickWindow>
#include <QFile>
#include <QMatrix4x4>
#include <QPointer>
#include <QtMath>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace {
QShader shader(const char* path) {
    QFile file(QString::fromUtf8(path));
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader{};
}
constexpr qint64 textureBudget = 256LL * 1024 * 1024;
}

class ImageRenderer final : public QQuickRhiItemRenderer {
    std::unique_ptr<QRhiBuffer> vertices, uniforms;
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    QRhi* device{};
    QImage image;
    qint64 imageKey{};
    bool upload{}, vertexUpload{}, nearest{}, checker{};
    QColor background;
    QSizeF viewport;
    qreal zoom{1}, dpr{1};
    QPointF pan;
    int rotation{};
    QPointer<ImageCanvas> owner;
    QString error;
    void fail(const QString& message) {
        if (error == message) return;
        error = message;
        const auto target=owner;
        if (target) QMetaObject::invokeMethod(target, [target,message] {
            if (target) target->reportRendererError(message);
        }, Qt::QueuedConnection);
    }
    void reset() {
        pipeline.reset(); bindings.reset(); texture.reset(); sampler.reset();
        uniforms.reset(); vertices.reset(); imageKey=0; vertexUpload=true; upload=true;
    }
protected:
    void synchronize(QQuickRhiItem* base) override {
        auto* item=static_cast<ImageCanvas*>(base);
        owner=item;
        if (image.cacheKey()!=item->m_image.cacheKey()) { image=item->m_image; upload=true; }
        if (nearest != item->m_nearest) {
            nearest=item->m_nearest; pipeline.reset(); bindings.reset(); sampler.reset();
        }
        viewport=item->size(); zoom=item->m_zoom; pan=item->m_pan;
        rotation=item->m_rotation; dpr=item->dpr(); background=item->m_background;
        checker=item->m_checkerboard;
    }
    void initialize(QRhiCommandBuffer*) override {
        if (device != rhi()) { reset(); device=rhi(); }
        // The render target and its compatible pass may change after resizing or screen changes.
        pipeline.reset();
    }
    void render(QRhiCommandBuffer* cb) override {
        QRhiResourceUpdateBatch* updates=rhi()->nextResourceUpdateBatch();
        bool drawable=!image.isNull();
        if (drawable && (image.format()!=QImage::Format_RGBA8888 || image.sizeInBytes()>textureBudget
            || image.width()>rhi()->resourceLimit(QRhi::TextureSizeMax)
            || image.height()>rhi()->resourceLimit(QRhi::TextureSizeMax))) {
            fail(QStringLiteral("Image exceeds this canvas's GPU limits or has an unsupported pixel format."));
            drawable=false;
        }
        if (!drawable) {
            // Do not retain a previous selection's VRAM when the selection is empty or rejected.
            pipeline.reset(); bindings.reset(); texture.reset(); imageKey=0;
        }
        if (drawable && (!texture || texture->pixelSize()!=image.size())) {
            pipeline.reset(); bindings.reset(); texture.reset();
            // Admission precedes allocation; at most one 256 MiB texture and its upload staging.
            texture.reset(rhi()->newTexture(QRhiTexture::RGBA8,image.size()));
            if (!texture->create()) { texture.reset(); drawable=false; fail(QStringLiteral("GPU texture allocation failed.")); }
            upload=true;
        }
        if (drawable && !vertices) {
            vertices.reset(rhi()->newBuffer(QRhiBuffer::Immutable,QRhiBuffer::VertexBuffer,16*sizeof(float)));
            uniforms.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,96));
            if (!vertices->create() || !uniforms->create()) {
                vertices.reset(); uniforms.reset(); drawable=false; fail(QStringLiteral("GPU buffer allocation failed."));
            }
            vertexUpload=true;
        }
        if (drawable && !sampler) {
            const auto filter=nearest?QRhiSampler::Nearest:QRhiSampler::Linear;
            sampler.reset(rhi()->newSampler(filter,filter,QRhiSampler::None,QRhiSampler::ClampToEdge,QRhiSampler::ClampToEdge));
            if (!sampler->create()) { sampler.reset(); drawable=false; fail(QStringLiteral("GPU sampler creation failed.")); }
        }
        if (drawable && !bindings) {
            bindings.reset(rhi()->newShaderResourceBindings());
            bindings->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(0,QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,uniforms.get()),
                QRhiShaderResourceBinding::sampledTexture(1,QRhiShaderResourceBinding::FragmentStage,texture.get(),sampler.get())});
            if (!bindings->create()) { bindings.reset(); drawable=false; fail(QStringLiteral("GPU resource binding failed.")); }
        }
        if (drawable && !pipeline) {
            auto vs=shader(":/shaders/src/render/image.vert.qsb");
            auto fs=shader(":/shaders/src/render/image.frag.qsb");
            pipeline.reset(rhi()->newGraphicsPipeline());
            pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
            pipeline->setShaderStages({{QRhiShaderStage::Vertex,vs},{QRhiShaderStage::Fragment,fs}});
            QRhiVertexInputLayout layout;
            layout.setBindings({{4*sizeof(float)}});
            layout.setAttributes({{0,0,QRhiVertexInputAttribute::Float2,0},{0,1,QRhiVertexInputAttribute::Float2,2*sizeof(float)}});
            pipeline->setVertexInputLayout(layout);
            pipeline->setShaderResourceBindings(bindings.get());
            pipeline->setSampleCount(renderTarget()->sampleCount());
            pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
            if (!vs.isValid() || !fs.isValid() || !pipeline->create()) {
                pipeline.reset(); drawable=false; fail(QStringLiteral("GPU shader pipeline unavailable. Check the graphics driver and shader resources."));
            }
        }
        if (drawable) {
            if (vertexUpload) {
                constexpr float data[]={-.5f,-.5f,0,0, .5f,-.5f,1,0, -.5f,.5f,0,1, .5f,.5f,1,1};
                updates->uploadStaticBuffer(vertices.get(),data); vertexUpload=false;
            }
            if (upload || imageKey!=image.cacheKey()) {
                updates->uploadTexture(texture.get(),image);
                upload=false; imageKey=image.cacheKey();
            }
            QMatrix4x4 projection;
            projection.ortho(0,float(viewport.width()),float(viewport.height()),0,-1,1);
            QMatrix4x4 model;
            model.translate(float(viewport.width()/2+pan.x()),float(viewport.height()/2+pan.y()));
            model.rotate(float(rotation),0,0,1);
            model.scale(float(image.width()*zoom/dpr),float(image.height()*zoom/dpr));
            const auto matrix=rhi()->clipSpaceCorrMatrix()*projection*model;
            float data[24]{};
            std::memcpy(data,matrix.constData(),64);
            data[16]=float(background.redF()); data[17]=float(background.greenF());
            data[18]=float(background.blueF()); data[19]=1;
            data[20]=checker?1.f:0.f;
            updates->updateDynamicBuffer(uniforms.get(),0,sizeof(data),data);
            fail({});
        }
        cb->beginPass(renderTarget(),background,{1.0f,0},updates);
        if (drawable) {
            cb->setGraphicsPipeline(pipeline.get());
            cb->setViewport({0,0,float(renderTarget()->pixelSize().width()),float(renderTarget()->pixelSize().height())});
            cb->setShaderResources(bindings.get());
            const QRhiCommandBuffer::VertexInput input(vertices.get(),0);
            cb->setVertexInput(0,1,&input);
            cb->draw(4);
        }
        cb->endPass();
    }
};

ImageCanvas::ImageCanvas(QQuickItem* parent):QQuickRhiItem(parent) {
    setAlphaBlending(false);
    connect(this,&QQuickRhiItem::effectiveColorBufferSizeChanged,this,[this] { refreshFit(); });
}
QQuickRhiItemRenderer* ImageCanvas::createRenderer() { return new ImageRenderer; }
qreal ImageCanvas::dpr() const { return window()?window()->effectiveDevicePixelRatio():1; }
void ImageCanvas::setImage(const QImage& value) {
    if (value.cacheKey()==m_image.cacheKey()) return;
    endLoupe(); m_image=value; m_rotation=0; m_pan={}; m_fitMode="shrink";
    reportRendererError({}); refreshFit(); emit imageChanged(); update();
}
void ImageCanvas::reportRendererError(const QString& value) {
    if (m_rendererError==value) return;
    m_rendererError=value; emit rendererErrorChanged();
}
void ImageCanvas::setBackground(QColor value) {
    if (!value.isValid() || value==m_background) return;
    m_background=value; emit appearanceChanged(); update();
}
void ImageCanvas::setCheckerboard(bool value) {
    if (m_checkerboard==value) return;
    m_checkerboard=value; emit appearanceChanged(); update();
}
void ImageCanvas::setNearest(bool value) {
    if (m_nearest==value) return;
    m_nearest=value; emit appearanceChanged(); update();
}
void ImageCanvas::refreshFit() {
    if (!m_image.isNull() && !m_fitMode.isEmpty() && width()>0 && height()>0) {
        const bool quarter=m_rotation%180!=0;
        const qreal iw=quarter?m_image.height():m_image.width();
        const qreal ih=quarter?m_image.width():m_image.height();
        const qreal x=width()*dpr()/iw, y=height()*dpr()/ih;
        if (m_fitMode=="actual") m_zoom=1;
        else if (m_fitMode=="width") m_zoom=x;
        else if (m_fitMode=="height") m_zoom=y;
        else if (m_fitMode=="fill") m_zoom=std::max(x,y);
        else m_zoom=m_fitMode=="fit"?std::min(x,y):std::min({x,y,qreal(1)});
        m_pan={};
    }
    emit viewChanged(); update();
}
void ImageCanvas::fit(const QString& mode) {
    endLoupe();
    if (mode!="shrink" && mode!="fit" && mode!="fill" && mode!="width" && mode!="height") return;
    m_fitMode=mode; refreshFit();
}
void ImageCanvas::actualSize() { endLoupe(); m_fitMode="actual"; refreshFit(); }
void ImageCanvas::zoomAt(qreal factor,qreal x,qreal y) {
    if (m_image.isNull() || !std::isfinite(factor) || factor<=0 || !std::isfinite(x) || !std::isfinite(y)) return;
    const qreal target=std::clamp(m_zoom*factor,qreal(.001),qreal(64));
    const qreal ratio=target/m_zoom;
    const QPointF anchor(x-width()/2,y-height()/2);
    m_pan=anchor-(anchor-m_pan)*ratio;
    m_zoom=target; m_fitMode.clear(); emit viewChanged(); update();
}
QPointF ImageCanvas::imagePointAt(qreal x,qreal y) const {
    if (m_image.isNull()) return {};
    const QPointF p=(QPointF(x-width()/2,y-height()/2)-m_pan)*(dpr()/m_zoom);
    const qreal radians=qDegreesToRadians(qreal(m_rotation));
    return {std::cos(radians)*p.x()+std::sin(radians)*p.y()+m_image.width()/2.0,
            -std::sin(radians)*p.x()+std::cos(radians)*p.y()+m_image.height()/2.0};
}
void ImageCanvas::panBy(qreal dx,qreal dy) {
    if (m_image.isNull() || !std::isfinite(dx) || !std::isfinite(dy)) return;
    m_pan+=QPointF(dx,dy); m_fitMode.clear(); emit viewChanged(); update();
}
void ImageCanvas::rotateClockwise() {
    endLoupe(); m_rotation=(m_rotation+90)%360;
    if (!m_fitMode.isEmpty()) refreshFit(); else { emit viewChanged(); update(); }
}
void ImageCanvas::beginLoupe(qreal x,qreal y) {
    if (m_loupe || m_image.isNull()) return;
    m_savedZoom=m_zoom; m_savedPan=m_pan; m_savedFit=m_fitMode; m_loupe=true;
    zoomAt(std::max(qreal(1),m_zoom)/m_zoom,x,y);
}
void ImageCanvas::endLoupe() {
    if (!m_loupe) return;
    m_zoom=m_savedZoom; m_pan=m_savedPan; m_fitMode=m_savedFit; m_loupe=false;
    emit viewChanged(); update();
}
void ImageCanvas::cycleBackground() {
    if (m_checkerboard) { setCheckerboard(false); setBackground(QColor("#191919")); }
    else if (m_background==QColor("#191919")) setBackground(Qt::black);
    else if (m_background==Qt::black) setBackground(QColor("#808080"));
    else if (m_background==QColor("#808080")) setBackground(Qt::white);
    else setCheckerboard(true);
}
void ImageCanvas::geometryChange(const QRectF& now,const QRectF& old) {
    QQuickRhiItem::geometryChange(now,old); refreshFit();
}


