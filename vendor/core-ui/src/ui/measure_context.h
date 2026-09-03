#pragma once

#include "renderer.h"

namespace ui {

class MeasureContext {
public:
    void BindRenderer(Renderer* renderer) { renderer_ = renderer; }
    Renderer* BoundRenderer() const { return renderer_; }

    float MeasureTextWidth(const std::wstring& text, float fontSize,
                           const wchar_t* family = nullptr,
                           DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) const {
        return renderer_ ? renderer_->MeasureTextWidth(text, fontSize, family, weight) : 0.0f;
    }

    float MeasureTextHeight(const std::wstring& text, float maxWidth, float fontSize,
                            DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) const {
        if (!renderer_) return fontSize * 1.3f;
        return renderer_->MeasureTextHeight(text, maxWidth, fontSize, weight);
    }

    ComPtr<IDWriteTextLayout> CreateTextLayout(const std::wstring& text,
                                               float maxWidth, float maxHeight,
                                               float fontSize,
                                               bool wrap = false,
                                               DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) const {
        ComPtr<IDWriteTextLayout> layout;
        if (!renderer_) return layout;
        return renderer_->CreateTextLayout(text, maxWidth, maxHeight, fontSize, wrap, weight);
    }

private:
    Renderer* renderer_ = nullptr;
};

extern MeasureContext* g_activeMeasureContext;

} // namespace ui
