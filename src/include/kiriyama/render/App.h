#pragma once

// 应用宿主：把 SDL 的样板全部吃掉。
//
// 之前每个 demo 都要抄一遍 SDL_Init / CreateWindowAndRenderer / 设置逻辑
// 分辨率 / 算 delta / 算 fps / 处理 QUIT / Esc / RenderPresent / 销毁。这些
// 和"要展示什么"毫无关系，属于基础设施，所以收进这一层：
//
//   class MyDemo final : public render::AppDelegate {
//       AppConfig config() const override { ... }   // 窗口多大、逻辑空间多大
//       bool onInit(App& app) override { ... }      // 一次性准备
//       void onFrame(App& app) override { ... }     // 只写绘制
//   };
//   KIRIYAMA_MAIN(MyDemo)                           // 见 AppMain.h
//
// 约定：
//   - 所有绘制坐标都在"逻辑空间"里，尺寸由 AppConfig::logicalWidth/Height 决定，
//     窗口缩放 / DPR 变化由 SDL 的 letterbox 映射，排版结果不受影响。
//   - 每帧开头宿主会自动 reset 画笔状态并铺 clearColor，onFrame 里不用自己清屏。
//   - 每帧结尾宿主会自动 RenderPresent，onFrame 里不要调。
//   - onFrame 的帧率由 vsync 控制（web 上即 requestAnimationFrame）。
//
// 这一层只做"宿主"：它不认识数据、场景树，也不做任何布局。
//
// 注意这里刻意不 include <SDL3/SDL_main.h>：在 emscripten 上它带一份
// header-only 的 main 实现，每个 include 它的翻译单元都会想定义 main。
// 入口适配放在 AppMain.h，只有真正定义 SDL_App* 的那个 .cc 才引入。

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <SDL3/SDL.h>

#include "Painter.h"
#include "TextRenderer.h"
#include "kiriyama/core/Color.h"
#include "kiriyama/core/Geometry.h"
#include "kiriyama/core/SceneStyle.h"

namespace kiriyama::render {

/// 宿主配置。全部有默认值，demo 只需要改想改的那几项。
struct AppConfig {
    std::string title{"kiriyama canvas"};

    /// 初始窗口大小（物理像素）。
    int windowWidth{960};
    int windowHeight{620};

    /// 逻辑空间大小：所有绘制坐标都在这个范围内。
    int logicalWidth{960};
    int logicalHeight{540};

    /// 每帧清屏色。
    core::Color clearColor{core::Color::Hex(0xFFFFFF)};

    /// 垂直同步。web 上开启即跟随浏览器刷新率。
    bool vsync{true};

    /// 高分屏超采样（SDL_WINDOW_HIGH_PIXEL_DENSITY）。
    /// 不开：canvas 物理像素 = CSS 像素，SDL_RenderGeometry 又没有 MSAA，
    /// 多边形边缘（饼图扇形 / 圆）在高分屏上是 1 像素硬切的锯齿。
    /// 开：canvas 物理像素 = CSS × devicePixelRatio，几何按物理像素光栅化，
    /// 边缘平滑一倍以上；文字纹理仍按逻辑字号光栅化后放大（略软，后续再做 DPI 感知）。
    bool highPixelDensity{false};

    SDL_WindowFlags windowFlags{SDL_WINDOW_RESIZABLE};
};

class App;

/// 应用钩子。派生它，实现 onFrame，然后 KIRIYAMA_MAIN(你的类)。
///
/// 只有 onFrame 是纯虚的：一个 demo 至少得画点东西。其余都有默认实现。
class AppDelegate {
public:
    virtual ~AppDelegate() = default;

    /// 启动前读取一次，之后不可变。
    [[nodiscard]] virtual AppConfig config() const { return AppConfig{}; }

    /// 窗口 / 渲染器 / 画笔都就绪后调用。返回 false 表示放弃启动。
    virtual bool onInit(App& app) {
        (void)app;
        return true;
    }

    /// 每帧调用。此时画布已清屏，画笔状态已复位。
    virtual void onFrame(App& app) = 0;

    /// 原始 SDL 事件，需要键盘 / 滚轮等细节时覆盖。
    /// QUIT 和 Esc 已由宿主处理，这里收不到（退出前会先走 onQuit）。
    virtual void onEvent(App& app, const SDL_Event& event) {
        (void)app;
        (void)event;
    }

    /// 销毁窗口之前调用，用来释放贴图等 GPU 资源。
    virtual void onQuit(App& app) { (void)app; }
};

/// SDL 宿主：窗口 + 渲染器 + 时钟 + 事件分发。
///
/// 同时充当绘制上下文：onFrame(App& app) 里 app.painter() / app.text()
/// 就是当前帧要用的画笔和文字渲染器。
class App {
public:
    /// 构造时就把 delegate 的 config 读进来，所以 start() 之前
    /// config() / canvas() 也已经可用（测试和无窗口环境需要）。
    explicit App(std::unique_ptr<AppDelegate> delegate);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// 读配置、初始化 SDL、建窗口和渲染器、调 onInit。
    /// 失败返回 false（已打日志，调用方直接退出即可）。
    bool start();

    /// 推进一帧：算时间、清屏、onFrame、present。
    /// 返回 false 表示宿主已经不可用，应当结束主循环。
    bool step();

    /// 分发一个 SDL 事件。返回 false 表示用户要求退出（QUIT / Esc）。
    bool dispatch(const SDL_Event& event);

    /// 释放资源。可重复调用；start() 失败时也会走到这里。
    void stop();

    // ------------------------------------------------------------ 上下文

    [[nodiscard]] bool running() const noexcept { return started_; }
    [[nodiscard]] const AppConfig& config() const noexcept { return config_; }

    [[nodiscard]] SDL_Window* window() const noexcept { return window_; }
    [[nodiscard]] SDL_Renderer* renderer() const noexcept { return renderer_; }
    [[nodiscard]] Painter& painter() noexcept { return *painter_; }
    [[nodiscard]] SdlTextRenderer& text() noexcept { return *text_; }

    /// 逻辑空间矩形，原点在左上角。布局基本都从这里算起。
    [[nodiscard]] core::RectD canvas() const noexcept;

    /// 自启动以来的秒数。
    [[nodiscard]] double time() const noexcept { return time_; }
    /// 上一帧耗时（秒）。
    [[nodiscard]] double deltaTime() const noexcept { return deltaTime_; }
    /// 平滑后的帧率，显示在页脚用。
    [[nodiscard]] double fps() const noexcept { return fps_; }
    /// 已渲染的帧数。
    [[nodiscard]] std::uint64_t frameIndex() const noexcept { return frameIndex_; }

    /// 指针位置（逻辑坐标），已经过 letterbox 变换。
    [[nodiscard]] core::Vec2d pointer() const noexcept { return pointer_; }
    [[nodiscard]] bool pointerDown() const noexcept { return pointerDown_; }

    // ------------------------------------------------------------ 光标

    /// 请求光标形状。每帧都可以无脑调：形状没变时不会碰 SDL。
    /// Inherit 等价于 Default（"跟随父节点"是场景树的概念，到宿主这一层已经没有祖先了）。
    /// 无人调用时保持上一次的形状，所以调用方只管在命中时设、没命中时设 Default。
    void setCursor(core::CursorShape shape);
    [[nodiscard]] core::CursorShape cursor() const noexcept { return cursor_; }

private:
    static constexpr std::size_t kCursorShapeCount =
        static_cast<std::size_t>(core::CursorShape::NotAllowed) + 1;

    void refreshPointer(float windowX, float windowY) noexcept;

    /// 懒创建 + 复用。SDL_Cursor 是窗口系统资源，不能每帧造。
    [[nodiscard]] SDL_Cursor* cursorHandle(core::CursorShape shape);

    std::unique_ptr<AppDelegate> delegate_;
    AppConfig config_{};

    SDL_Window* window_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    std::unique_ptr<Painter> painter_;
    std::unique_ptr<SdlTextRenderer> text_;

    std::uint64_t lastCounter_{0};
    std::uint64_t frequency_{1};
    std::uint64_t frameIndex_{0};
    double time_{0.0};
    double deltaTime_{0.0};
    double fps_{0.0};

    core::Vec2d pointer_{};
    bool pointerDown_{false};
    bool started_{false};
    bool ttfReady_{false};

    std::array<SDL_Cursor*, kCursorShapeCount> cursors_{};
    core::CursorShape cursor_{core::CursorShape::Default};
};

}  // namespace kiriyama::render
