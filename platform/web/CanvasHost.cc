// 唯一宿主（web 平台）：一个 wasm 里装一套组件类型，页面想画几个就创建几个实例。
//
// 这不是"demo 宿主"：本文件不认识"演示"这个概念，只认组件实例。
//   · 页面 kiriyama_create_component("pie") 拿句柄 → kiriyama_show_component(句柄, x, y, w, h)
//     把它挂到画布上。两个饼图就是两个句柄，各存各的数据与样式；
//   · 数据与样式全部走 C API 的 load*/set*（entry.js 从 model / config 翻译），
//     第一个参数永远是句柄；
//   · 本文件只做：同步实例集合 → 每帧 pump → tick / draw → 气泡与光标。
//
// 一个产物：kiriyama_canvas.js / .wasm / .data。再加组件不新增产物，只多一份
// 组件实现——字体和渲染层只下载一次。

#include "kiriyama/render/AppMain.h"

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

#include "kiriyama/core/Color.h"
#include "kiriyama/core/Geometry.h"
#include "kiriyama/core/SceneNode.h"
#include "kiriyama/render/Font.h"
#include "kiriyama/render/Fonts.generated.h"
#include "kiriyama/render/SceneDraw.h"
#include "kiriyama/render/SceneHost.h"
#include "kiriyama/render/TooltipHost.h"
#include "kiriyama/render/TtfFont.h"
#include "kiriyama/scene/LineChart.h"
#include "kiriyama/scene/PieChart.h"
#include "kiriyama/scene/SparkBar.h"
#include "kiriyama/scene/TableWidget.h"
#include "kiriyama/web/c_api.h"

namespace {

namespace core = kiriyama::core;

using kiriyama::core::NodeType;
using kiriyama::core::RectD;
using kiriyama::core::SceneNode;
using kiriyama::render::App;
using kiriyama::render::AppConfig;
using kiriyama::render::Font;
using kiriyama::render::FontFace;
using kiriyama::render::FontLibrary;
using kiriyama::render::TooltipHost;
using kiriyama::render::drawSceneGeometry;
using kiriyama::render::frameContext;
using kiriyama::scene::LineChart;
using kiriyama::scene::LinePointNode;
using kiriyama::scene::PieChart;
using kiriyama::scene::SparkBar;
using kiriyama::scene::TableStyle;
using kiriyama::scene::TableWidget;
using kiriyama::web::Component;
using kiriyama::web::InstanceInfo;

std::shared_ptr<const FontFace> gBodyFace;

/// 一个实例 = 一个组件节点。所有组件（Pie / SparkBar / Table / Line）都不再需要专属 Renderer：
/// 几何统一走 drawSceneGeometry（渲染树通用遍历），文本统一走 TextRenderer::drawScene。
struct Instance {
    Component type{Component::Pie};

    /// 节点由场景树持有，这里只留裸指针。
    PieChart* pie{nullptr};
    SparkBar* spark{nullptr};
    TableWidget* table{nullptr};
    LineChart* line{nullptr};
};

class CanvasHost final : public kiriyama::render::AppDelegate {
public:
    AppConfig config() const override {
        AppConfig config;
        config.title = "kiriyama/canvas";
        // web 上窗口尺寸就是 canvas 的 CSS 尺寸，必须和逻辑空间一致，
        // 否则 SDL 会把窗口高写回 canvas style、顶掉页面的 aspect-ratio。
        config.windowWidth = 960;
        config.windowHeight = 540;
        config.logicalWidth = 960;
        config.logicalHeight = 540;
        // 高分屏 / 系统缩放下 canvas 物理像素 = CSS × DPR：几何按物理像素光栅化，
        // 多边形边缘的锯齿显著减轻（SDL_RenderGeometry 没有 MSAA）。
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

        // 所有实例挂在同一棵根下：tooltip 找目标要遍历树，一页一个气泡。
        root_ = std::make_unique<SceneNode>(NodeType::Group, "root");

        // 气泡一页一个（语义互斥），配置却按实例走：指针落在谁身上就喂谁的。
        tooltip_ = std::make_unique<TooltipHost>(app);
        return true;
    }

    void onEvent(App& app, const SDL_Event& event) override {
        if (root_ != nullptr) {
            (void)kiriyama::render::dispatchInputEvent(app, event, *root_, &controller_);
        }
    }

    void onFrame(App& app) override {
        syncInstances(app);

        const auto ctx = frameContext(app);

        // pending 数据 / 样式先落地：全部实例都 pump 一次（没脏就返回），
        // 这样未挂载 / 隐藏期间收到的数据，show 出来就是最新的。
        for (const auto& [handle, instance] : instances_) {
            pumpOne(handle, *instance);
        }

        // 不可见的实例不 tick：否则它照样记 hover、弹气泡。
        for (const auto& [handle, instance] : instances_) {
            if (kiriyama::scene::Widget* node = nodeOf(*instance);
                node != nullptr && node->visible()) {
                node->tickTree(ctx);
            }
        }

        // 主题是页面级状态：所有实例共用一份，切主题不重建任何场景树。
        const core::ChartTheme& theme = kiriyama::web::currentTheme();

        // 画布底色跟随当前主题（SparkBar 等透明底组件与多组件间距由此统一铺底）。
        app.painter().color(theme.background).clear();

        for (const auto& [handle, instance] : instances_) {
            if (const kiriyama::scene::Widget* node = nodeOf(*instance);
                node != nullptr && node->visible()) {
                // 所有组件 = 渲染树：几何走通用图元遍历，文本走 TextRenderer，
                // 渲染层不再按图表类型各写一个 Renderer。
                drawSceneGeometry(app.painter(), *node, theme);
                app.text().drawScene(*node, theme);
            }
        }

        updateCursor(app);

        for (const auto& [handle, instance] : instances_) {
            if (instance->pie != nullptr) {
                const bool hovered = instance->pie->visible() &&
                                     instance->pie->hoveredIndex() != core::kInvalidIndex;
                kiriyama::web::reportHoveredSlice(
                    handle, hovered ? static_cast<int>(instance->pie->hoveredIndex()) : -1);
            } else if (instance->line != nullptr) {
                const LinePointNode* hoveredNode =
                    instance->line->visible() ? instance->line->hoveredPointNode() : nullptr;
                kiriyama::web::reportHoveredLinePoint(
                    handle,
                    hoveredNode != nullptr ? static_cast<int>(hoveredNode->seriesIndex()) : -1,
                    hoveredNode != nullptr ? static_cast<int>(hoveredNode->pointIndex()) : -1);
            }
        }

        // 气泡：指针落在哪个实例上就用哪个的气泡配置，最后画，盖在所有内容上面。
        if (const int active = instanceUnderPointer(app); active != 0) {
            kiriyama::web::applyTooltip(active, *tooltip_);
        }
        tooltip_->update(*root_, ctx);
    }

private:
    [[nodiscard]] static TableStyle makeTableStyle() {
        TableStyle style;
        style.headerFontSize = 10.5;
        style.cellFontSize = 12.0;
        style.metrics.headerHeight = 36.0;
        style.metrics.rowHeight = 31.0;
        style.metrics.paddingX = 14.0;
        return style;
    }

    /// 按 C API 那边的实例集合增删节点。页面只改状态，不碰场景树。
    ///
    /// 关键：句柄单调递增不回收，但如果页面清空后再用回同一个逻辑"位置"，
    /// 新实例会拿到新句柄。真正需要重建的还有**类型变了**的情况。
    void syncInstances(App& app) {
        const std::vector<InstanceInfo> desired = kiriyama::web::instances();

        // 先删：句柄没了，或者句柄还在但类型变了。
        std::vector<int> gone;
        for (const auto& [handle, instance] : instances_) {
            auto found = std::find_if(desired.begin(), desired.end(),
                [handle](const InstanceInfo& info) { return info.handle == handle; });
            if (found == desired.end() || found->type != instance->type) {
                gone.push_back(handle);
            }
        }
        for (const int handle : gone) {
            const auto it = instances_.find(handle);
            if (it != instances_.end()) {
                root_->removeChild(nodeOf(*it->second));
                instances_.erase(it);
            }
        }

        for (const InstanceInfo& info : desired) {
            const auto it = instances_.find(info.handle);
            if (it == instances_.end()) {
                instances_.emplace(info.handle, createInstance(app, info.type));
            }
            applyPlacement(*instances_.at(info.handle), info);
        }
    }

    /// 建一个实例：节点当场挂到 root_ 上（所有权交给树）。
    std::unique_ptr<Instance> createInstance(App& app, Component type) {
        auto instance = std::make_unique<Instance>();
        instance->type = type;

        switch (type) {
            case Component::Pie: {
                auto node = std::make_unique<PieChart>("pie");
                instance->pie = node.get();
                // 描述树文本定位用真实字体度量（不接就是 core 的估宽）。
                node->setTextMeasure(
                    [&textRenderer = app.text()](std::string_view text, double fontSize) {
                        const kiriyama::render::TextLayout layout = textRenderer.measure(
                            text, Font{Font::builtinFace(), fontSize}, {});
                        return core::TextMetrics{layout.boxWidth, layout.height};
                    });
                root_->appendChild(std::move(node));
                break;
            }
            case Component::SparkBar: {
                auto node = std::make_unique<SparkBar>("sparkbar");
                instance->spark = node.get();
                root_->appendChild(std::move(node));
                break;
            }
            case Component::Table: {
                auto node = std::make_unique<TableWidget>("table");
                node->setTextMeasure(
                    [&textRenderer = app.text()](std::string_view text, double fontSize) {
                        const kiriyama::render::TextLayout layout = textRenderer.measure(
                            text, Font{Font::builtinFace(), fontSize}, {});
                        return core::TextMetrics{layout.boxWidth, layout.height};
                    });
                node->setStyle(makeTableStyle());
                instance->table = node.get();
                root_->appendChild(std::move(node));
                break;
            }
            case Component::Line: {
                auto node = std::make_unique<LineChart>("line");
                node->setTextMeasure(
                    [&textRenderer = app.text()](std::string_view text, double fontSize) {
                        const auto& face = textRenderer.defaultFace()
                                               ? textRenderer.defaultFace()
                                               : Font::builtinFace();
                        const kiriyama::render::TextLayout layout =
                            textRenderer.measure(text, Font{face, fontSize}, {});
                        return core::TextMetrics{layout.boxWidth, layout.height};
                    });
                instance->line = node.get();
                root_->appendChild(std::move(node));
                break;
            }
        }
        return instance;
    }

    /// 把页面设的可见性 / 矩形落到节点上：位置走组件自身的 transform_，尺寸走局部 frame_。
    static void applyPlacement(Instance& instance, const InstanceInfo& info) {
        kiriyama::scene::Widget* node = nodeOf(instance);
        if (node == nullptr) {
            return;
        }
        if (node->visible() != info.visible) {
            node->setVisible(info.visible);
        }
        if (!info.visible) {
            return;
        }
        node->setTranslation(info.frame.origin());
        const RectD localFrame{0.0, 0.0, info.frame.width, info.frame.height};
        if (!(node->frame() == localFrame)) {
            node->setFrame(localFrame);
        }
    }

    static void pumpOne(int handle, Instance& instance) {
        if (instance.pie != nullptr) {
            kiriyama::web::pumpPie(handle, *instance.pie);
        } else if (instance.spark != nullptr) {
            kiriyama::web::pumpSparkBar(handle, *instance.spark);
        } else if (instance.table != nullptr) {
            kiriyama::web::pumpTable(handle, *instance.table);
        } else if (instance.line != nullptr) {
            kiriyama::web::pumpLine(handle, *instance.line);
        }
    }

    [[nodiscard]] static kiriyama::scene::Widget* nodeOf(Instance& instance) {
        if (instance.pie != nullptr) {
            return instance.pie;
        }
        if (instance.spark != nullptr) {
            return instance.spark;
        }
        if (instance.table != nullptr) {
            return instance.table;
        }
        return instance.line;
    }

    /// 指针落在哪个实例上（0 = 没有）。重叠时取后挂的那个（画在上面）。
    [[nodiscard]] int instanceUnderPointer(App& app) const {
        const kiriyama::core::Vec2d pointer = app.pointer();
        int found = 0;
        for (const auto& [handle, instance] : instances_) {
            const kiriyama::scene::Widget* node = nodeOf(*instance);
            if (node == nullptr || !node->visible()) {
                continue;
            }
            if (node->hitBounds().contains(pointer)) {
                found = handle;
            }
        }
        return found;
    }

    void updateCursor(App& app) {
        auto cursor = kiriyama::core::CursorShape::Default;
        const int active = instanceUnderPointer(app);
        const auto it = instances_.find(active);
        if (it != instances_.end()) {
            if (const kiriyama::scene::Widget* node = nodeOf(*it->second);
                node != nullptr && node->visible()) {
                cursor = node->cursorAt(app.pointer());
            }
        }
        app.setCursor(cursor);
    }

    std::unique_ptr<SceneNode> root_{};
    /// 句柄 → 实例。有序，所以绘制顺序 = 挂载顺序（后面盖前面）。
    std::map<int, std::unique_ptr<Instance>> instances_{};
    std::unique_ptr<TooltipHost> tooltip_{};
    kiriyama::scene::InputController controller_{};
};

}  // namespace

KIRIYAMA_MAIN(CanvasHost)
