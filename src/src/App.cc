#include "kiriyama/render/App.h"

#include <utility>

#include <SDL3_ttf/SDL_ttf.h>

namespace kiriyama::render {
namespace {

/// fps 指数平滑系数：越小越稳，越大越跟手。
constexpr double kFpsSmoothing = 0.1;

SDL_SystemCursor toSystemCursor(core::CursorShape shape) noexcept {
    using core::CursorShape;
    switch (shape) {
        case CursorShape::Pointer:
            return SDL_SYSTEM_CURSOR_POINTER;
        case CursorShape::Text:
            return SDL_SYSTEM_CURSOR_TEXT;
        case CursorShape::ResizeH:
            return SDL_SYSTEM_CURSOR_COL_RESIZE;
        case CursorShape::ResizeV:
            return SDL_SYSTEM_CURSOR_ROW_RESIZE;
        case CursorShape::Grab:
            return SDL_SYSTEM_CURSOR_GRAB;
        case CursorShape::Grabbing:
            return SDL_SYSTEM_CURSOR_GRABBING;
        case CursorShape::NotAllowed:
            return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
        case CursorShape::Inherit:
        case CursorShape::Default:
        default:
            return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

}  // namespace

App::App(std::unique_ptr<AppDelegate> delegate)
    : delegate_(std::move(delegate)), config_(delegate_ ? delegate_->config() : AppConfig{}) {}

App::~App() { stop(); }

bool App::start() {
    if (started_) {
        return true;
    }
    if (!delegate_) {
        SDL_Log("kiriyama: App 需要一个 delegate");
        return false;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("kiriyama: SDL_Init 失败: %s", SDL_GetError());
        return false;
    }

    // 字体是可选能力：初始化失败不阻塞启动，渲染层会回退到内置点阵字体。
    if (!TTF_Init()) {
        SDL_Log("kiriyama: TTF_Init 失败，将回退到点阵字体: %s", SDL_GetError());
    } else {
        ttfReady_ = true;
    }

    // highPixelDensity 必须在创建时生效（emscripten 后端按它决定 canvas 是否乘 DPR）。
    const SDL_WindowFlags flags = config_.highPixelDensity
        ? SDL_WindowFlags(config_.windowFlags | SDL_WINDOW_HIGH_PIXEL_DENSITY)
        : config_.windowFlags;
    if (!SDL_CreateWindowAndRenderer(config_.title.c_str(), config_.windowWidth,
                                     config_.windowHeight, flags, &window_,
                                     &renderer_)) {
        SDL_Log("kiriyama: 创建窗口/渲染器失败: %s", SDL_GetError());
        stop();
        return false;
    }

    if (config_.vsync) {
        SDL_SetRenderVSync(renderer_, 1);
    }

    // 逻辑空间固定，窗口尺寸交给 SDL 等比缩放（letterbox）。
    // 这样排版只跟 logicalWidth/Height 有关，浏览器缩放、DPR、全屏都不影响。
    SDL_SetRenderLogicalPresentation(renderer_, config_.logicalWidth, config_.logicalHeight,
                                    SDL_LOGICAL_PRESENTATION_LETTERBOX);

    painter_ = std::make_unique<Painter>(renderer_);
    text_ = std::make_unique<SdlTextRenderer>(renderer_);

    frequency_ = SDL_GetPerformanceFrequency();
    lastCounter_ = SDL_GetPerformanceCounter();
    started_ = true;
    frameIndex_ = 0;
    time_ = 0.0;
    deltaTime_ = 0.0;
    fps_ = 0.0;

    if (!delegate_->onInit(*this)) {
        SDL_Log("kiriyama: onInit 返回 false，放弃启动");
        stop();
        return false;
    }
    return true;
}

bool App::step() {
    if (!started_ || renderer_ == nullptr) {
        return false;
    }

    const std::uint64_t now = SDL_GetPerformanceCounter();
    const double elapsed = frequency_ != 0
                               ? static_cast<double>(now - lastCounter_) /
                                     static_cast<double>(frequency_)
                               : 0.0;
    lastCounter_ = now;
    deltaTime_ = elapsed;
    time_ += elapsed;
    if (elapsed > 1e-6) {
        const double instant = 1.0 / elapsed;
        fps_ = fps_ <= 0.0 ? instant : fps_ * (1.0 - kFpsSmoothing) + instant * kFpsSmoothing;
    }
    ++frameIndex_;

    text_->resetCounters();

    // 每帧从干净状态开始：上一帧残留的 opacity / 线宽不该影响这一帧。
    painter_->reset();
    painter_->color(config_.clearColor).clear();

    delegate_->onFrame(*this);

    SDL_RenderPresent(renderer_);
    return true;
}

bool App::dispatch(const SDL_Event& event) {
    if (!started_) {
        return false;
    }

    switch (event.type) {
        case SDL_EVENT_QUIT:
            return false;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_ESCAPE) {
                return false;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            refreshPointer(event.motion.x, event.motion.y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            refreshPointer(event.button.x, event.button.y);
            if (event.button.button == SDL_BUTTON_LEFT) {
                pointerDown_ = true;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            refreshPointer(event.button.x, event.button.y);
            if (event.button.button == SDL_BUTTON_LEFT) {
                pointerDown_ = false;
            }
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            pointerDown_ = false;
            break;
        default:
            break;
    }

    if (delegate_) {
        delegate_->onEvent(*this, event);
    }
    return true;
}

SDL_Cursor* App::cursorHandle(core::CursorShape shape) {
    const auto index = static_cast<std::size_t>(shape);
    if (index >= cursors_.size()) {
        return nullptr;
    }
    if (cursors_[index] == nullptr) {
        cursors_[index] = SDL_CreateSystemCursor(toSystemCursor(shape));
    }
    return cursors_[index];
}

void App::setCursor(core::CursorShape shape) {
    if (shape == core::CursorShape::Inherit) {
        shape = core::CursorShape::Default;
    }
    if (shape == cursor_) {
        return;
    }
    cursor_ = shape;

    if (window_ == nullptr) {
        return;
    }
    // 创建失败时传 nullptr 给 SDL_SetCursor 会回落到默认箭头，不崩。
    SDL_SetCursor(cursorHandle(shape));
}

void App::stop() {
    if (delegate_ && started_) {
        delegate_->onQuit(*this);
    }

    // 先放掉持有 renderer 的对象，再销毁 renderer 本身。
    text_.reset();
    painter_.reset();

    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (ttfReady_) {
        TTF_Quit();
        ttfReady_ = false;
    }

    // 光标是窗口系统资源，必须在 SDL_Quit 之前放掉。
    for (SDL_Cursor*& slot : cursors_) {
        if (slot != nullptr) {
            SDL_DestroyCursor(slot);
            slot = nullptr;
        }
    }
    cursor_ = core::CursorShape::Default;

    if (started_) {
        SDL_Quit();
    }

    started_ = false;
    frameIndex_ = 0;
    time_ = 0.0;
    deltaTime_ = 0.0;
    fps_ = 0.0;
    pointer_ = {};
    pointerDown_ = false;
}

core::RectD App::canvas() const noexcept {
    return core::RectD{0.0, 0.0, static_cast<double>(config_.logicalWidth),
                       static_cast<double>(config_.logicalHeight)};
}

void App::refreshPointer(float windowX, float windowY) noexcept {
    float x = windowX;
    float y = windowY;
    if (renderer_ != nullptr) {
        // 窗口坐标 → 逻辑坐标。letterbox 的偏移和缩放都在这一步吃掉。
        (void)SDL_RenderCoordinatesFromWindow(renderer_, windowX, windowY, &x, &y);
    }
    pointer_ = core::Vec2d{static_cast<double>(x), static_cast<double>(y)};
}

}  // namespace kiriyama::render
