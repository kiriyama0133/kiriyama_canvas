// Pie 组件宿主：数据由 Module.loadPie 喂进来，外观由 entry.js 通过下面这组
// kiriyama_pie_set_* setter 直接设置。C++ 侧不解析任何样式协议——
// options 对象 → setter 的映射在 entry.js 里，前端交付的是 JS，不是 C++。
//
// 数据协议（entry.js 的 encodePie 生成）：
//   label␟value    每行一片；空值 = 缺失，该片不画、不参与占比
//
// 这里没有兜底数据、没有写死的样式、也没有画布内的标题页脚——那些都是页面的事。
// 换数据走 Module.loadPie({ slices, options })，不用重新编译。

#include "kiriyama/render/AppMain.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "kiriyama/core/Color.h"
#include "kiriyama/core/Data.h"
#include "kiriyama/core/Layout.h"
#include "kiriyama/render/Font.h"
#include "kiriyama/render/Fonts.generated.h"
#include "kiriyama/render/PieChartRenderer.h"
#include "kiriyama/render/SceneHost.h"
#include "kiriyama/render/TtfFont.h"
#include "kiriyama/render/TooltipHost.h"
#include "kiriyama/scene/PieChart.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define KIRIYAMA_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define KIRIYAMA_EXPORT extern "C"
#endif

namespace {

using kiriyama::core::Color;
using kiriyama::core::PieChartData;
using kiriyama::core::RectD;
using kiriyama::render::App;
using kiriyama::render::AppConfig;
using kiriyama::render::Font;
using kiriyama::render::FontFace;
using kiriyama::render::FontLibrary;
using kiriyama::render::PieChartRenderer;
using kiriyama::render::PieStyle;
using kiriyama::render::TooltipHost;
using kiriyama::render::frameContext;
using kiriyama::scene::PieChart;

/// 字段分隔符：U+001F，与 entry.js 同一个约定。
constexpr char kUnitSeparator = '\x1f';

std::shared_ptr<const FontFace> gBodyFace;

[[nodiscard]] std::string_view field(std::string_view line, std::size_t index) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < index; ++i) {
        const std::size_t end = line.find(kUnitSeparator, start);
        if (end == std::string_view::npos) {
            return {};
        }
        start = end + 1;
    }
    const std::size_t end = line.find(kUnitSeparator, start);
    return end == std::string_view::npos ? line.substr(start) : line.substr(start, end - start);
}

[[nodiscard]] PieChartData parseModel(std::string_view text) {
    PieChartData data;

    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::string_view line =
            newline == std::string_view::npos ? text : text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

        if (line.empty()) {
            continue;
        }
        const std::string label(field(line, 0));
        const std::string valueText(field(line, 1));
        if (label.empty() || valueText.empty()) {
            continue;  // 空标签 / 空值 = 缺失，直接跳过
        }
        data.slices.push_back({label, std::strtod(valueText.c_str(), nullptr)});
    }

    return data;
}

// ---------------------------------------------------------------- 收件箱

struct Incoming {
    PieChartData data;
    std::uint64_t version{0};
    bool fresh{false};
};

Incoming gIncoming;

/// entry.js 的 setter 写这里（dirty 时下一帧应用到组件）。
/// 外观参数 lives here 而不是组件里：宿主还没建好时（wasm 初始化早期）setter 也能收。
struct PieConfig {
    PieStyle style;
    kiriyama::core::PieLayoutOptions layout;
    double explode{6.0};
    bool dirty{false};
};

PieConfig gConfig;

}  // namespace

// ---------------------------------------------------------------- 导出：数据

/// 页面调用的唯一数据入口（经 entry.js 的 Module.loadPie）。
/// 只做解析和登记，真正的替换发生在下一帧（避免在 JS 调用栈上动场景树）。
KIRIYAMA_EXPORT void kiriyama_pie_load(const char* text) {
    if (text == nullptr) {
        return;
    }
    gIncoming.data = parseModel(text);
    ++gIncoming.version;
    gIncoming.fresh = true;
}

// ---------------------------------------------------------------- 导出：外观
// entry.js 把页面的 options 对象逐项翻译成这些调用。颜色约定：
// 0..0xFFFFFF = 0xRRGGBB，负值 = 全透明（JS 侧 'none' 映射成 -1）。

KIRIYAMA_EXPORT void kiriyama_pie_set_label_font_size(double value) {
    gConfig.style.labelFontSize = value;
    gConfig.dirty = true;
}

KIRIYAMA_EXPORT void kiriyama_pie_set_label_color(double rgb) {
    gConfig.style.labelColor =
        rgb < 0.0 ? Color::Rgba(0.0, 0.0, 0.0, 0.0) : Color::Hex(static_cast<std::uint32_t>(rgb));
    gConfig.dirty = true;
}

KIRIYAMA_EXPORT void kiriyama_pie_set_show_labels(double value) {
    gConfig.style.showLabels = value != 0.0;
    gConfig.dirty = true;
}

KIRIYAMA_EXPORT void kiriyama_pie_set_show_values(double value) {
    gConfig.style.showValues = value != 0.0;
    gConfig.dirty = true;
}

KIRIYAMA_EXPORT void kiriyama_pie_set_gap_width(double value) {
    gConfig.style.gapWidth = value;
    gConfig.dirty = true;
}

KIRIYAMA_EXPORT void kiriyama_pie_set_gap_color(double rgb) {
    gConfig.style.gapColor =
        rgb < 0.0 ? Color::Rgba(0.0, 0.0, 0.0, 0.0) : Color::Hex(static_cast<std::uint32_t>(rgb));
    gConfig.dirty = true;
}

KIRIYAMA_EXPORT void kiriyama_pie_set_explode(double value) {
    gConfig.explode = value;
    gConfig.dirty = true;
}

KIRIYAMA_EXPORT void kiriyama_pie_set_inner_radius(double value) {
    gConfig.layout.innerRadiusRatio = value;
    gConfig.dirty = true;
}

KIRIYAMA_EXPORT void kiriyama_pie_set_start_angle(double degrees) {
    gConfig.layout.startAngle = degrees * (kiriyama::math::kPi / 180.0);
    gConfig.dirty = true;
}

KIRIYAMA_EXPORT void kiriyama_pie_set_label_offset(double value) {
    gConfig.layout.labelOffset = value;
    gConfig.dirty = true;
}

namespace {

// ---------------------------------------------------------------- 宿主

class PieHost final : public kiriyama::render::AppDelegate {
public:
    AppConfig config() const override {
        AppConfig config;
        config.title = "kiriyama/canvas - Pie";
        // web 上窗口尺寸就是 canvas 的 CSS 尺寸，必须和逻辑空间一致，
        // 否则 SDL 会把窗口高写回 canvas style、顶掉 shell 的 aspect-ratio。
        config.windowWidth = 960;
        config.windowHeight = 540;
        config.logicalWidth = 960;
        config.logicalHeight = 540;
        // 高分屏 / 系统缩放下 canvas 物理像素 = CSS × DPR：几何按物理像素光栅化，
        // 扇形边缘的锯齿显著减轻（SDL_RenderGeometry 没有 MSAA，1x 就是硬边）。
        config.highPixelDensity = true;
        return config;
    }

    bool onInit(App& app) override {
        if (!kiriyama::render::embedded::kBodyFont.empty()) {
            gBodyFace = FontLibrary::shared().load(kiriyama::render::embedded::kBodyFont);
        }
        if (gBodyFace) {
            app.text().setDefaultFace(gBodyFace);
        }

        chart_ = std::make_unique<PieChart>("pie");
        chart_->setFrame(RectD{128.0, 64.0, 704.0, 412.0});

        renderer_ = std::make_unique<PieChartRenderer>(app.painter(), app.text());
        renderer_->setStyle(gConfig.style);

        tooltip_ = std::make_unique<TooltipHost>(app);
        return true;
    }

    void onFrame(App& app) override {
        adoptIncoming();
        applyStyleIfDirty();

        chart_->tickTree(frameContext(app));
        renderer_->draw(*chart_);

        // 悬停即交互区：手型提示。
        app.setCursor(chart_->hoveredSlice() != nullptr ? kiriyama::core::CursorShape::Pointer
                                                        : kiriyama::core::CursorShape::Default);

        // 气泡最后画：盖在所有内容上面。扇区是 TooltipTarget，宿主自己会找。
        tooltip_->update(*chart_, frameContext(app));
    }

private:
    void adoptIncoming() {
        if (!gIncoming.fresh) {
            return;
        }
        data_ = std::move(gIncoming.data);
        gIncoming.data = {};
        gIncoming.fresh = false;
        chart_->setData(&data_, gIncoming.version);
    }

    void applyStyleIfDirty() {
        if (!gConfig.dirty) {
            return;
        }
        gConfig.dirty = false;
        renderer_->setStyle(gConfig.style);
        chart_->setLayoutOptions(gConfig.layout);
        chart_->setSliceDefaults(gConfig.explode);
    }

    PieChartData data_{};
    std::unique_ptr<PieChart> chart_{};
    std::unique_ptr<PieChartRenderer> renderer_{};
    std::unique_ptr<TooltipHost> tooltip_{};
};

}  // namespace

KIRIYAMA_MAIN(PieHost)
