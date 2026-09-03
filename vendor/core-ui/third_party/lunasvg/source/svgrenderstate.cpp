#include "svgrenderstate.h"
#include "svgfilter.h"

#include <algorithm>
#include <cmath>

namespace lunasvg {

SVGBlendInfo::SVGBlendInfo(const SVGElement* element)
    : m_clipper(element->clipper())
    , m_masker(element->masker())
    , m_filter(element->filter())
    , m_opacity(element->opacity())
{
}

bool SVGBlendInfo::requiresCompositing(SVGRenderMode mode) const
{
    return (m_clipper && m_clipper->requiresMasking())
        || (mode == SVGRenderMode::Painting && (m_masker || m_filter || m_opacity < 1.f));
}

bool SVGRenderState::hasCycleReference(const SVGElement* element) const
{
    auto current = this;
    do {
        if(element == current->element())
            return true;
        current = current->parent();
    } while(current);
    return false;
}

void SVGRenderState::beginGroup(const SVGBlendInfo& blendInfo)
{
    auto requiresCompositing = blendInfo.requiresCompositing(m_mode);
    if(requiresCompositing) {
        auto paintBox = m_element->paintBoundingBox();
        // gh-svg: filter region — 离屏 bbox 在用户单位下按滤镜外延膨胀, 否则
        // blur/drop-shadow 会被裁掉。margin = max(|dx|,|dy|) + 3σ。
        if(blendInfo.filter()) {
            auto effect = blendInfo.filter()->effect();
            if(effect.type != SVGFilterEffectType::None) {
                float margin = std::max(std::abs(effect.dx), std::abs(effect.dy))
                             + 3.f * effect.stdDeviation;
                paintBox.inflate(margin);
            }
        }
        auto boundingBox = m_currentTransform.mapRect(paintBox);
        boundingBox.intersect(m_canvas->extents());
        m_canvas = Canvas::create(boundingBox);
    } else {
        m_canvas->save();
    }

    if(!requiresCompositing && blendInfo.clipper()) {
        blendInfo.clipper()->applyClipPath(*this);
    }
}

void SVGRenderState::endGroup(const SVGBlendInfo& blendInfo)
{
    if(m_canvas == m_parent->canvas()) {
        m_canvas->restore();
        return;
    }

    auto opacity = m_mode == SVGRenderMode::Clipping ? 1.f : blendInfo.opacity();
    if(blendInfo.clipper())
        blendInfo.clipper()->applyClipMask(*this);
    if(m_mode == SVGRenderMode::Painting && blendInfo.masker()) {
        blendInfo.masker()->applyMask(*this);
    }

    // gh-svg: 滤镜在 blend 回父画布前对离屏 surface 应用。
    // dx/dy/σ 由用户单位 × currentTransform 缩放转设备像素。
    if(m_mode == SVGRenderMode::Painting && blendInfo.filter()) {
        auto effect = blendInfo.filter()->effect();
        const float sx = m_currentTransform.xScale();
        const float sy = m_currentTransform.yScale();
        const float sigma = effect.stdDeviation * (sx + sy) * 0.5f;
        if(effect.type == SVGFilterEffectType::SourcePaintBlur) {
            applyGaussianBlur(*m_canvas, sigma);
        } else if(effect.type == SVGFilterEffectType::RecolorSourceAlpha) {
            applyDropShadow(*m_canvas, effect.dx * sx, effect.dy * sy,
                            sigma, effect.color, effect.opacity);
        }
    }

    m_parent->m_canvas->blendCanvas(*m_canvas, BlendMode::Src_Over, opacity);
}

} // namespace lunasvg
