// Text 组件演示：SDL3 + WebAssembly。
//
// 页面分两栏：
//   左：字号阶梯、字距、图标字体
//   右：水平对齐、软换行、省略号截断、场景树文本节点
// 文字全部经由 SdlTextRenderer 绘制（SDL_ttf 光栅化 + 字形纹理缓存），
// 行数 / 字形数 / 缓存规模在页脚实时显示。
//
// 交互：把鼠标停在其中一块上会弹 tooltip（悬停 350ms）；按 Tab / Shift+Tab 在
// 同样的目标之间轮转键盘焦点，焦点目标是立即弹出的。tooltip 的定位与显隐全在
// core::Tooltip 里，这里只负责"每帧告诉它指针在哪、焦点在哪、然后画出来"。
//
// SDL 的一切（初始化、窗口、渲染器、时钟、present、销毁）都在 render::App 里，
// 这个文件只回答三个问题：窗口多大、初始化时准备什么、每帧画什么。

#include "kiriyama/render/AppMain.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "kiriyama/core/Color.h"
#include "kiriyama/core/NodeType.h"
#include "kiriyama/core/SceneNode.h"
#include "kiriyama/core/Tooltip.h"
#include "kiriyama/render/Font.h"
#include "kiriyama/render/Fonts.generated.h"
#include "kiriyama/render/Painter.h"
#include "kiriyama/render/TextLayout.h"
#include "kiriyama/render/TextRenderer.h"
#include "kiriyama/render/TooltipRenderer.h"
#include "kiriyama/render/TtfFont.h"
#include "kiriyama/render/SceneHost.h"
#include "kiriyama/scene/Animation.h"
#include "kiriyama/scene/Drag.h"

namespace {

using kiriyama::core::Color;
using kiriyama::core::NodeType;
using kiriyama::core::RectD;
using kiriyama::core::SceneNode;
using kiriyama::core::SceneStyle;
using kiriyama::core::TextAlign;
using kiriyama::core::TextBaseline;
using kiriyama::render::App;
using kiriyama::render::AppConfig;
using kiriyama::render::Font;
using kiriyama::render::FontFace;
using kiriyama::render::FontLibrary;
using kiriyama::render::frameContext;
using kiriyama::render::Painter;
using kiriyama::render::SdlTextRenderer;
using kiriyama::render::TextLayoutOptions;
using kiriyama::render::TooltipRenderer;

// ---------------------------------------------------------------- 配色
// 手头是设计稿色值时用 Color::Hex(0xRRGGBB / 0xRRGGBBAA)；
// 从配置里读进来的字符串用 Color::FromHex("#RRGGBB")（可带 alpha）。

constexpr Color kBg = Color::Hex(0xF7F7F5);
constexpr Color kCard = Color::Hex(0xFFFFFF);
constexpr Color kBorder = Color::Hex(0xE0DED6);
constexpr Color kInk = Color::Hex(0x2C2C2A);
constexpr Color kMuted = Color::Hex(0x8A8A84);
constexpr Color kAccent = Color::Hex(0x3E6DB5);
constexpr Color kTeal = Color::Hex(0x2E8A85);
constexpr Color kAmber = Color::Hex(0xC2762A);

// ---------------------------------------------------------------- 字体
// 具体带哪几支字体由构建参数决定（-DKIRIYAMA_FONTS / scripts/build.*），
// 这里只读生成好的清单：kBodyFont 是正文字体，kIconFont 是图标字体（可能为空）。
// 加载失败时 body()/icon() 会退回内置点阵字体，页面仍然能出图。

namespace embedded = kiriyama::render::embedded;

std::shared_ptr<const FontFace> gBodyFace;
std::shared_ptr<const FontFace> gIconFace;

[[nodiscard]] Font body(double pixelSize) {
    return Font(gBodyFace ? gBodyFace : Font::builtinFace(), pixelSize);
}

[[nodiscard]] Font icon(double pixelSize) {
    return Font(gIconFace ? gIconFace : Font::builtinFace(), pixelSize);
}

// ---------------------------------------------------------------- tooltip 目标
// 每帧重填：绘制时顺手把"可悬停的矩形 + 文案"登记进来，画完再统一做命中测试。
// id 按登记顺序发，顺序稳定 → id 稳定，状态机的"同一个目标"判断才成立。

struct TipTarget {
    std::uint64_t id{0};
    RectD rect{};
    std::string title;
    std::string body;
};

std::vector<TipTarget> gTips;
std::uint64_t gNextTipId{1};

void addTip(const RectD& rect, std::string_view title, std::string_view body) {
    gTips.push_back(TipTarget{gNextTipId++, rect, std::string(title), std::string(body)});
}

[[nodiscard]] const TipTarget* findTip(std::uint64_t id) {
    for (const TipTarget& tip : gTips) {
        if (tip.id == id) {
            return &tip;
        }
    }
    return nullptr;
}

/// FontAwesome 4.7 的私有区码点，写成 UTF-8 字节避免源码编码歧义。
constexpr std::string_view kIcons[] = {
    "\xEF\x82\x80",  // U+F080 bar-chart
    "\xEF\x89\x81",  // U+F201 line-chart
    "\xEF\x88\x80",  // U+F200 pie-chart
    "\xEF\x87\xBE",  // U+F1FE area-chart
    "\xEF\x83\xA4",  // U+F0E4 dashboard
    "\xEF\x80\x93",  // U+F013 gear
};

// 场景树：三个文本节点，其中一个在 opacity=0.45 的组里，验证透明度继承。
std::unique_ptr<SceneNode> makeLegend() {
    auto root = std::make_unique<SceneNode>(NodeType::Group, "legend");

    auto row = [](std::string key, std::string text, double y, const Color& color) {
        auto node = std::make_unique<SceneNode>(NodeType::Text, std::move(key));
        node->setFrame(RectD{0.0, y, 380.0, 16.0});
        node->setText(std::move(text));
        SceneStyle style;
        style.fill = color;
        style.fontSize = 12.0;
        style.textAlign = TextAlign::Left;
        style.textBaseline = TextBaseline::Top;
        node->setStyle(style);
        return node;
    };

    root->appendChild(row("t.revenue", "Scene node  revenue  +12.4%", 0.0, kAccent));

    auto dimmed = std::make_unique<SceneNode>(NodeType::Group, "dimmed");
    {
        SceneStyle style;
        style.opacity = 0.45;
        dimmed->setStyle(style);
    }
    dimmed->appendChild(row("t.cost", "Scene node  cost      -3.1%", 18.0, kTeal));
    root->appendChild(std::move(dimmed));

    root->appendChild(row("t.profit", "Scene node  profit    +8.9%", 36.0, kAmber));
    return root;
}

// ---------------------------------------------------------------- 拖拽示范
// 一块能拖的小卡片，组件是叠出来的：
//
//   Drag(位置) → Animation(透明度) → 两行 Text
//
// 指针的按下 / 移动 / 抬起全由 scene::Widget 记账，Drag 只处理"拖"，
// Animation 只处理"淡"，两个组件各写各的节点，不会抢同一个 transform。

constexpr double kChipWidth = 178.0;
constexpr double kChipHeight = 48.0;

std::unique_ptr<kiriyama::scene::Animation> makeChipContent() {
    auto content = std::make_unique<kiriyama::scene::Animation>("chip-opacity");
    content->setFrame(RectD{0.0, 0.0, kChipWidth, kChipHeight});
    content->setDefaultDuration(0.18);

    auto line = [](std::string key, std::string text, double y, double size, const Color& color) {
        auto node = std::make_unique<SceneNode>(NodeType::Text, std::move(key));
        node->setFrame(RectD{12.0, y, kChipWidth - 20.0, 16.0});
        node->setText(std::move(text));
        SceneStyle style;
        style.fill = color;
        style.fontSize = size;
        style.textAlign = TextAlign::Left;
        style.textBaseline = TextBaseline::Top;
        node->setStyle(style);
        return node;
    };

    content->appendChild(line("chip.title", "drag me", 8.0, 13.0, kAccent));
    content->appendChild(line("chip.sub", "Animation + Drag", 27.0, 10.0, kMuted));
    return content;
}

// ---------------------------------------------------------------- 各区块
// 每个函数返回自己占用的高度，方便上下排列。

void drawSectionLabel(App& app, std::string_view label, double x, double y) {
    TextLayoutOptions options;
    app.text().drawText(label, body(10.0), {x, y}, options, kMuted);
}

double drawHeader(App& app, double x, double y, double contentWidth) {
    SdlTextRenderer& text = app.text();
    TextLayoutOptions options;
    text.drawText("Text component", body(26.0), {x, y}, options, kInk);

    char embeddedCount[32];
    std::snprintf(embeddedCount, sizeof(embeddedCount), "%zu embedded", embedded::kCount);
    const std::string subtitle =
        std::string("SDL_ttf  -  ") + std::string(gBodyFace ? gBodyFace->family() : "builtin bitmap") +
        "  -  " + embeddedCount + " font(s)  -  wasm32-unknown-emscripten";
    text.drawText(subtitle, body(11.0), {x, y + 46.0}, options, kMuted);
    app.painter().color(kBorder).line({x, y + 70.0}, {x + contentWidth, y + 70.0});
    return 78.0;
}

double drawSizes(App& app, double x, double y, double width) {
    SdlTextRenderer& text = app.text();
    drawSectionLabel(app, "SIZE  -  px", x, y);

    const double sizes[] = {9.0, 11.0, 14.0, 20.0, 28.0};
    double cursor = y + 22.0;
    for (const double size : sizes) {
        char tag[16];
        std::snprintf(tag, sizeof(tag), "%2.0f", size);
        text.drawText(tag, body(9.0), {x, cursor + (size > 14.0 ? 8.0 : 3.0)}, {}, kMuted);

        // 有 maxWidth + ellipsis 兜底：宁可截断，也不允许压到隔壁栏
        TextLayoutOptions options;
        options.maxWidth = width - 32.0;
        options.wrap = false;
        options.maxLines = 1;
        options.ellipsis = "..";
        text.drawText("AaBbCc 0123", body(size), {x + 28.0, cursor}, options, kInk);

        // 整行都是悬停区（含左边那个字号标签）
        const double rowHeight = std::max(22.0, size + 6.0);
        const auto metrics = body(size).metrics();
        char title[32];
        char detail[128];
        std::snprintf(title, sizeof(title), "font-size %.0f px", size);
        std::snprintf(detail, sizeof(detail), "ascent %.1f    descent %.1f    line %.1f",
                      metrics.ascent, metrics.descent, metrics.lineHeight());
        addTip(RectD{x, cursor - 2.0, width, rowHeight}, title, detail);

        cursor += std::max(24.0, size + 10.0);
    }
    return cursor - y;
}

double drawTracking(App& app, double x, double y, double width) {
    SdlTextRenderer& text = app.text();
    drawSectionLabel(app, "TRACKING  -  letter-spacing", x, y);

    text.drawText("TRACKING 0.0", body(12.0), {x, y + 22.0}, {}, kInk);

    TextLayoutOptions loose;
    loose.letterSpacing = 3.0;
    loose.maxWidth = width;
    loose.wrap = false;
    text.drawText("TRACKING 3.0", body(12.0), {x, y + 40.0}, loose, kAccent);
    return 62.0;
}

double drawIcons(App& app, double x, double y, double width) {
    if (!gIconFace) {
        // 没打包图标字体时给一句怎么打开它的提示；高度保持一致，免得上下列跳动。
        drawSectionLabel(app, "ICONS  -  not embedded", x, y);
        app.text().drawText("scripts/build.sh fontawesome-webfont", body(11.0), {x, y + 24.0}, {},
                            kMuted);
        return 58.0;
    }

    const std::string label = std::string("ICONS  -  ") + std::string(embedded::kIconFont);
    drawSectionLabel(app, label, x, y);

    double pen = x;
    const Color tints[] = {kAccent, kTeal, kAmber, kAccent, kTeal, kAmber};
    for (std::size_t i = 0; i < std::size(kIcons); ++i) {
        // 图标字体自己一份 Font，和正文互不影响
        app.text().drawText(kIcons[i], icon(24.0), {pen, y + 20.0}, {}, tints[i]);
        addTip(RectD{pen, y + 16.0, 30.0, 34.0}, "icon glyph",
               "FontAwesome 4.7 private-use codepoint");
        pen += 34.0;
    }
    return 58.0;
}

double drawAlignment(App& app, double x, double y, double width) {
    SdlTextRenderer& text = app.text();
    Painter& painter = app.painter();
    drawSectionLabel(app, "ALIGN  -  left / center / right", x, y);

    const double boxWidth = (width - 16.0) / 3.0;
    const double top = y + 22.0;
    const char* samples[] = {"Left", "Center", "Right"};
    const TextAlign aligns[] = {TextAlign::Left, TextAlign::Center, TextAlign::Right};
    const char* tipTitles[] = {"TextAlign::Left", "TextAlign::Center", "TextAlign::Right"};
    const char* tipBodies[] = {"text block pinned to the left edge of the frame",
                               "text block centered inside the frame",
                               "text block pinned to the right edge of the frame"};

    for (int i = 0; i < 3; ++i) {
        const RectD box{x + static_cast<double>(i) * (boxWidth + 8.0), top, boxWidth, 36.0};
        painter.color(kCard).fillRoundRect(box, 6.0).color(kBorder).strokeRoundRect(box, 6.0);

        TextLayoutOptions options;
        options.maxWidth = box.width - 16.0;
        options.wrap = false;
        options.align = aligns[i];
        text.drawText(samples[i], body(12.0), {box.x + 8.0, box.y + 11.0}, options, kInk);

        addTip(box, tipTitles[i], tipBodies[i]);
    }
    return 56.0;
}

double drawWrap(App& app, double x, double y, double width) {
    SdlTextRenderer& text = app.text();
    drawSectionLabel(app, "WRAP  -  greedy, breaks on spaces", x, y);

    constexpr std::string_view kParagraph =
        "Greedy line breaking fills each line until the next word would overflow, then backs "
        "off to the last break opportunity. Line height is 17px here.";

    TextLayoutOptions options;
    options.maxWidth = width;
    options.wrap = true;
    options.lineHeight = 17.0;

    const Font font = body(12.0);
    const kiriyama::render::TextLayout layout = text.measure(kParagraph, font, options);

    const RectD guide{x, y + 22.0, width, layout.height};
    app.painter().color(kBorder).strokeRect(guide);
    text.drawText(kParagraph, font, {x, y + 22.0}, options, kInk);

    char detail[96];
    std::snprintf(detail, sizeof(detail), "%zu lines   %.0f px tall   maxWidth %.0f",
                  layout.lineCount(), layout.height, options.maxWidth);
    addTip(guide, "TextLayout::layout", detail);
    return 20.0 + layout.height;
}

double drawEllipsis(App& app, double x, double y, double width) {
    SdlTextRenderer& text = app.text();
    drawSectionLabel(app, "TRUNCATE  -  maxLines = 2 + ellipsis", x, y);

    TextLayoutOptions options;
    options.maxWidth = width;
    options.wrap = true;
    options.lineHeight = 17.0;
    options.maxLines = 2;
    options.ellipsis = "...";
    const RectD box = text.drawText(
        "When a label must stay inside a fixed box, clamping to two lines and adding an "
        "ellipsis keeps the layout stable instead of overflowing into the next row.",
        body(12.0), {x, y + 22.0}, options, kAccent);

    addTip(box, "maxLines = 2", "the last line is truncated with an ellipsis");
    return 20.0 + 34.0;
}

double drawLegend(App& app, SceneNode& legend, double x, double y) {
    drawSectionLabel(app, "SCENE TREE  -  drawScene(root)", x, y);
    legend.setTranslation({x, y + 22.0});
    app.text().drawScene(legend);

    // 三行对应 makeLegend() 里的三个 Text 节点（第二行在 opacity=0.45 的组里）
    const char* names[] = {"revenue", "cost", "profit"};
    const char* values[] = {"+12.4%    series 1", "-3.1%     group opacity 0.45",
                            "+8.9%     series 3"};
    for (int i = 0; i < 3; ++i) {
        addTip(RectD{x, y + 22.0 + static_cast<double>(i) * 18.0, 380.0, 16.0}, names[i],
               values[i]);
    }
    return 20.0 + 54.0;
}

void drawFooter(App& app, double x, double y, double width) {
    SdlTextRenderer& text = app.text();
    app.painter().color(kBorder).line({x, y - 12.0}, {x + width, y - 12.0});

    const RectD canvas = app.canvas();
    char info[320];
    std::snprintf(info, sizeof(info),
                  "%.0f x %.0f   |   %.0f fps   |   %llu lines   |   %llu glyphs   |   "
                  "%zu cached   |   hover / Tab for tooltip",
                  canvas.width, canvas.height, app.fps(),
                  static_cast<unsigned long long>(text.lineCount()),
                  static_cast<unsigned long long>(text.glyphCount()), text.glyphCacheSize());

    TextLayoutOptions options;
    options.maxWidth = width;
    options.wrap = false;
    text.drawText(info, body(10.0), {x, y}, options, kMuted);
}

// ---------------------------------------------------------------- 应用

class TextDemo final : public kiriyama::render::AppDelegate {
public:
    [[nodiscard]] AppConfig config() const override {
        AppConfig config;
        config.title = "kiriyama canvas - text";
        config.windowWidth = 960;
        config.windowHeight = 620;
        config.logicalWidth = 960;
        config.logicalHeight = 540;
        config.clearColor = kBg;
        return config;
    }

    bool onInit(App& app) override {
        FontLibrary& fonts = FontLibrary::shared();

        // 只加载本次构建真的带进来的字体；清单为空的那一项直接跳过。
        if (!embedded::kBodyFont.empty()) {
            gBodyFace = fonts.load(embedded::kBodyFont);
        }
        if (!embedded::kIconFont.empty()) {
            gIconFace = fonts.load(embedded::kIconFont);
        }

        // 场景树里的 Text 节点只带 fontSize，字体面从渲染器上取。
        if (gBodyFace) {
            app.text().setDefaultFace(gBodyFace);
        }

        tipRenderer_ = std::make_unique<TooltipRenderer>(app.painter(), app.text());
        if (gBodyFace) {
            tipRenderer_->setFace(gBodyFace);
        }

        legend_ = makeLegend();

        // 可拖卡片：Drag 管位置，里面套一层 Animation 管透明度。
        const RectD canvas = app.canvas();
        const double contentWidth = std::min(canvas.width - 80.0, 880.0);

        chipDrag_ = std::make_unique<kiriyama::scene::Drag>("chip");
        chipDrag_->setFrame(RectD{0.0, 0.0, kChipWidth, kChipHeight});
        chipDrag_->setTranslation({(canvas.width - contentWidth) * 0.5, canvas.height - 112.0});

        kiriyama::scene::DragOptions dragOptions;
        dragOptions.bounds = RectD{8.0, 8.0, canvas.width - 16.0, canvas.height - 16.0};
        chipDrag_->setOptions(dragOptions);

        std::unique_ptr<kiriyama::scene::Animation> content = makeChipContent();
        chipAnim_ = content.get();
        chipAnim_->mutableStyle().opacity = 0.0;
        chipAnim_->toOpacity(1.0, {.duration = 0.45, .delay = 0.15});
        chipDrag_->appendChild(std::move(content));

        // 拖动中变淡。这一句每次移动都会执行，Animation 会把重复请求丢掉，
        // 所以调用方不用自己记"是不是已经在淡了"。
        chipDrag_->onDrag([this](const kiriyama::scene::Drag&) {
            if (chipAnim_ != nullptr) {
                chipAnim_->toOpacity(0.62, {.duration = 0.12});
            }
        });
        chipDrag_->onEnd([this](const kiriyama::scene::Drag&) {
            if (chipAnim_ != nullptr) {
                chipAnim_->toOpacity(1.0, {.duration = 0.22});
            }
        });

        return true;
    }

    void onEvent(App& app, const SDL_Event& event) override {
        (void)app;
        if (event.type == SDL_EVENT_KEY_DOWN) {
            // Tab / Shift+Tab 轮转焦点目标；真正的切换放到 onFrame 里做，
            // 因为目标矩形是绘制时才算出来的。
            if (event.key.key == SDLK_TAB) {
                focusStep_ = (event.key.mod & SDL_KMOD_SHIFT) != 0 ? -1 : 1;
            }
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            // 换回鼠标操作就松开键盘焦点，不然 Tab 过一次就再也点不动了。
            releaseFocus_ = true;
        }
    }

    void onFrame(App& app) override {
        gTips.clear();
        gNextTipId = 1;

        // 光标每帧先归位，谁命中谁再改。不这么做的话，指针移出可点区域后
        // 手型会一直黏在那里（SDL 只在 setCursor 时才改光标）。
        app.setCursor(kiriyama::core::CursorShape::Default);

        const RectD canvas = app.canvas();
        const double width = canvas.width;
        const double height = canvas.height;

        const double margin = 40.0;
        const double contentWidth = std::min(width - margin * 2.0, 880.0);
        const double contentX = (width - contentWidth) * 0.5;
        const double top = margin + 78.0;

        drawHeader(app, contentX, margin, contentWidth);

        const double gap = 32.0;
        const double leftWidth = contentWidth * 0.46;
        const double rightWidth = contentWidth - leftWidth - gap;
        const double rightX = contentX + leftWidth + gap;

        double leftY = top;
        leftY += drawSizes(app, contentX, leftY, leftWidth) + 20.0;
        leftY += drawTracking(app, contentX, leftY, leftWidth) + 8.0;
        drawIcons(app, contentX, leftY, leftWidth);

        double rightY = top;
        rightY += drawAlignment(app, rightX, rightY, rightWidth) + 20.0;
        rightY += drawWrap(app, rightX, rightY, rightWidth) + 20.0;
        rightY += drawEllipsis(app, rightX, rightY, rightWidth) + 20.0;
        drawLegend(app, *legend_, rightX, rightY);

        drawFooter(app, contentX, height - 24.0, contentWidth);

        // 可拖卡片：先按指针推进，再画（画的时候才需要新位置）。
        updateChip(app);
        drawChip(app);

        // tooltip 最后画：它在所有内容之上。
        updateTooltip(app);
    }

private:
    /// 指针记账全在基类，这里只需要把这一帧的上下文喂进去。
    void updateChip(App& app) {
        if (chipDrag_ == nullptr) {
            return;
        }
        chipDrag_->tickTree(frameContext(app));  // 自己 + 里面的 Animation

        // 光标由节点自己声明：可抓 → 抓着。这样"能不能拖"不用调用方重复判断命中。
        using kiriyama::core::CursorShape;
        if (chipDrag_->pressed()) {
            app.setCursor(CursorShape::Grabbing);
        } else if (chipDrag_->pointerInside()) {
            app.setCursor(CursorShape::Grab);
        }
    }

    void drawChip(App& app) {
        if (chipDrag_ == nullptr) {
            return;
        }
        const RectD box = chipDrag_->hitBounds();
        const double alpha = chipAnim_ != nullptr ? chipAnim_->style().opacity : 1.0;

        Painter& painter = app.painter();
        painter.color(kCard, alpha).fillRoundRect(box, 10.0);
        painter.color(kBorder, alpha).strokeWidth(1.0).strokeRoundRect(box, 10.0);

        // 整棵子树（Animation → 两行字）一次画完，透明度沿途继承。
        app.text().drawScene(*chipDrag_);
    }

    /// 每帧把指针 / 焦点喂给状态机，可见就画出来。
    void updateTooltip(App& app) {
        const RectD canvas = app.canvas();
        const double now = app.time() * 1000.0;

        // ---- 键盘焦点
        // 焦点环把 -1（没有焦点）也算一环：Tab 走完最后一个再按一次就松开，
        // 键盘用户才有办法退出，而不是被永久黏在某个元素上。
        if (releaseFocus_) {
            focusIndex_ = -1;
            releaseFocus_ = false;
        }
        if (focusStep_ != 0) {
            const int count = static_cast<int>(gTips.size());
            if (count > 0) {
                const int cycle = count + 1;
                int slot = focusIndex_ + 1;  // 无焦点(-1) 记作 0
                slot = ((slot + focusStep_) % cycle + cycle) % cycle;
                focusIndex_ = slot - 1;  // 走回 0 就是松开
            } else {
                focusIndex_ = -1;
            }
            focusStep_ = 0;
        }

        const bool hasFocus =
            focusIndex_ >= 0 && focusIndex_ < static_cast<int>(gTips.size());
        if (hasFocus) {
            const TipTarget& target = gTips[static_cast<std::size_t>(focusIndex_)];
            // 每帧重喂一次：布局变了锚点要跟着走，id 不变就不会重置延迟
            tooltip_.focus(target.id, target.rect, now);

            // 焦点环 —— 键盘用户得看得见当前选中的是谁
            const RectD ring{target.rect.x - 3.0, target.rect.y - 3.0, target.rect.width + 6.0,
                             target.rect.height + 6.0};
            app.painter().color(kAccent).strokeWidth(1.0).strokeRoundRect(ring, 4.0);
        } else {
            tooltip_.blur(now);
        }

        // ---- 鼠标悬停（后登记的在上层）
        const kiriyama::core::Vec2d pointer = app.pointer();
        const TipTarget* hovered = nullptr;
        if (canvas.contains(pointer)) {
            for (auto it = gTips.rbegin(); it != gTips.rend(); ++it) {
                if (it->rect.contains(pointer)) {
                    hovered = &*it;
                    break;
                }
            }
        }
        if (hovered != nullptr) {
            tooltip_.hover(hovered->id, hovered->rect, now);
        } else {
            tooltip_.hover(0, RectD{}, now);
        }

        // 按下就收起，免得挡住拖拽 / 点击的目标
        if (app.pointerDown()) {
            (void)tooltip_.press(now);
        }

        (void)tooltip_.update(now);
        if (!tooltip_.visible() || !tipRenderer_) {
            return;
        }

        const TipTarget* shown = findTip(tooltip_.targetId());
        if (shown == nullptr) {
            return;
        }

        (void)tipRenderer_->draw(tooltip_.anchor(), {shown->title, shown->body}, canvas,
                                tooltip_.opacity(now));
    }

    std::unique_ptr<SceneNode> legend_;
    std::unique_ptr<kiriyama::scene::Drag> chipDrag_;
    kiriyama::scene::Animation* chipAnim_{nullptr};
    std::unique_ptr<TooltipRenderer> tipRenderer_;
    kiriyama::core::TooltipState tooltip_;
    int focusIndex_{-1};
    int focusStep_{0};
    bool releaseFocus_{false};
};

}  // namespace

KIRIYAMA_MAIN(TextDemo)
