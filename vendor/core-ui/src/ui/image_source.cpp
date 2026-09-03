#include "image_source.h"
#include "renderer.h"
#include <cwctype>

namespace ui {

// 各派生类的外部构造函数（在对应 cpp 实现）
std::unique_ptr<IImageSource> CreateBitmapSourceFromFile(const std::wstring& path, Renderer& r);
std::unique_ptr<IImageSource> CreateBitmapSourceFromPixels(const void* px, int w, int h, int stride, Renderer& r);

std::unique_ptr<IImageSource> CreateGifSourceFromFile(const std::wstring& path, Renderer& r);
std::unique_ptr<IImageSource> CreateSvgSourceFromFile(const std::wstring& path, Renderer& r);
std::unique_ptr<IImageSource> CreateTiledSource(int fullW, int fullH, int tileSize, Renderer& r);

static std::wstring ExtLower(const std::wstring& path) {
    auto dot = path.rfind(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring ext = path.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)std::towlower(c);
    return ext;
}

std::unique_ptr<IImageSource>
IImageSource::CreateFromFile(const std::wstring& path, Renderer& r) {
    std::wstring ext = ExtLower(path);

    // SVG：走矢量
    if (ext == L"svg" || ext == L"svgz") {
        return CreateSvgSourceFromFile(path, r);
    }

    // GIF：先尝试动画源；如果只有 1 帧（单帧 GIF）或打开失败就回退到位图
    if (ext == L"gif") {
        auto gif = CreateGifSourceFromFile(path, r);
        if (gif && gif->FrameCount() > 1) return gif;
        // 单帧回退
    }

    // 其它：静态位图（WIC 支持的一切）
    return CreateBitmapSourceFromFile(path, r);
}

std::unique_ptr<IImageSource>
IImageSource::CreateFromPixels(const void* pixels, int w, int h, int stride, Renderer& r) {
    return CreateBitmapSourceFromPixels(pixels, w, h, stride, r);
}

std::unique_ptr<IImageSource>
IImageSource::CreateTiled(int fullW, int fullH, int tileSize, Renderer& r) {
    return CreateTiledSource(fullW, fullH, tileSize, r);
}

} // namespace ui
