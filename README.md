# kiriyama/canvas

SDL3 + WebAssembly 的 2D 画布组件库。核心是一棵纯逻辑的场景树（`core`），
上面叠一层 SDL 后端（`render`），再往上是你自己的应用。

本文分两部分：**怎么编译**、**前端（渲染层）怎么用**。

---

## 1. 环境要求

| 依赖 | 说明 |
| --- | --- |
| Emscripten (emsdk) | 提供 `emcc` / wasm32 工具链。脚本会自动在仓库同级、`/e/emsdk`、`~/emsdk` 等常见位置找，找不到会让你手动 `source emsdk_env.sh` |
| CMake ≥ 3.20 + Ninja | `build/` 目录是 Ninja 工程 |
| Node.js | 只用来跑 wasm 单元测试（gtest 编译成 wasm 后用 node 执行） |
| 任意静态服务器 | 预览用；`file://` 打不开 wasm |

字体、FreeType、googletest 都躺在 `third_parts/` 里，不需要额外装。

---

## 2. 编译

两个脚本参数完全一致，Windows 用 `.ps1`，Linux / macOS / Git Bash 用 `.sh`。

### 2.1 基本用法

```powershell
# Windows PowerShell
.\scripts\build.ps1
```

```bash
# Linux / macOS / Git Bash
./scripts/build.sh
```

不带参数就是**默认构建**：Debug、只打包 `JetBrainsMono-Light` 这一支字体、产物落在 `web/`。

### 2.2 选择打包哪些字体

位置参数就是字体名（对应 `public/` 下的文件，可省略扩展名，不分大小写）：

```bash
./scripts/build.sh JetBrainsMono-Bold
./scripts/build.sh JetBrainsMono-Light fontawesome-webfont
./scripts/build.sh --all              # public/ 下所有字体
```

字体清单会同时喂给两处，所以"构建带了什么"和"运行时能读到什么"永远一致：

- `--preload-file` —— 把 ttf 放进 wasm 虚拟文件系统 `/public/<文件名>`；
- `build/generated/kiriyama/render/Fonts.generated.h` —— 生成 `embedded::kFonts` / `kBodyFont` / `kIconFont`，应用和测试都读它。

> 名字写错会直接 CMake 报错并列出 `public/` 里实际有哪些字体，不会静默漏打包。

### 2.3 全部参数

| 参数 | 作用 |
| --- | --- |
| `字体名 ...` | 要打包的字体，可多个、可逗号分隔 |
| `--all` | 打包 `public/` 下全部字体 |
| `--release` / `--debug` | 构建类型（默认 Debug） |
| `--test` | 顺带构建并运行单元测试 |
| `--clean` | 先删掉 `build/` 再配 |
| `-j N` / `--jobs N` | 并行任务数 |
| `-h` / `--help` | 打印帮助 |

### 2.4 不用脚本，直接 CMake

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DKIRIYAMA_FONTS="JetBrainsMono-Light;fontawesome-webfont"
cmake --build build
```

`KIRIYAMA_FONTS` 用分号分隔，可带扩展名也可不带。

### 2.5 产物与预览

```
web/index.html     外壳页面（web/shell.html 生成的）
web/index.js       JS 胶水
web/index.wasm     11MB 左右
web/index.data     字体，体积随打包字体数量变化
```

```bash
python -m http.server 8765 --bind 127.0.0.1 --directory web
# 打开 http://127.0.0.1:8765/index.html
```

---

## 3. 测试

```bash
./scripts/build.sh --test          # 构建 + 跑 src 层测试
```

三套测试都是 gtest 编译成 wasm、用 node 执行：

| 测试 | 数量 | 跑法 |
| --- | --- | --- |
| `math` | 49 | `ninja -C build kiriyama_math_tests`，然后在 `build/math/tests` 里 `node kiriyama_math_tests.js` |
| `core` | 239 | `ninja -C build kiriyama_core_tests`，然后在 `build/core/tests` 里 `node kiriyama_core_tests.js` |
| `src` | 95 | `./scripts/build.sh --test`，或 `ninja -C build kiriyama_text_tests` + 在 `build/src/tests` 里 `node kiriyama_text_tests.js` |

必须**在产物所在目录**运行：`.data` 是按当前工作目录找的。

---

## 4. 分层

```
math/   纯数学：向量、矩阵、插值、缓动曲线、弹簧、刻度。无状态、可 constexpr
core/   场景树、几何、命中测试、布局、Tooltip 状态机、表格排序状态机。不依赖 SDL，可在 node 里测
src/    scene/  交互基类 Widget、动画、拖拽（只依赖 core，仍可在 node 里测）
        render/ SDL 后端：App 主循环、Painter、文字渲染、Tooltip 绘制、光标
        app/    示例程序
```

一句话：**core 里不许出现 SDL，src/render 里不许出现业务分支**。

已确立的范式是 **值类型模型 + 有状态控制器 + 无状态渲染器**：
`TableData` 配 `TableState`，`Dataset` 配 `SceneBuilder`，再加上 `VirtualScroll` /
`InteractionState` / `TooltipState` 这些控制器，渲染层只读结果。表格和图表都走
`buildScene()` → `reconcileScene()` → `drawScene()`，不各写一套。

---

## 5. 前端使用

下面全部是渲染层（`src/`）的用法。

### 5.1 一个最小应用

```cpp
#include "kiriyama/render/AppMain.h"

using namespace kiriyama;

class MyApp final : public render::AppDelegate {
public:
    render::AppConfig config() const override {
        render::AppConfig config;
        config.title = "my canvas";
        config.logicalWidth = 960;    // 所有绘制坐标都在这个空间里
        config.logicalHeight = 540;
        config.clearColor = core::Color::Hex(0xF7F7F5);
        return config;
    }

    bool onInit(render::App& app) override {
        // 一次性准备：加载字体、建节点、起动画
        return true;
    }

    void onFrame(render::App& app) override {
        // 每帧：只写绘制
        app.painter().color(core::Color::Hex(0x2C2C2A)).fillRect({40, 40, 200, 80});
    }
};

KIRIYAMA_MAIN(MyApp)
```

`KIRIYAMA_MAIN` 里有函数定义，**一个可执行文件只能用一次**，且必须写在 `.cc` 里。

窗口尺寸变化时逻辑坐标系不变，`App` 负责等比缩放 + letterbox，所以你不用自己算 DPI。

### 5.2 画图：Painter

链式调用，状态（颜色 / 透明度 / 线宽）会保持到下段代码或 `reset()`：

```cpp
render::Painter& p = app.painter();
p.color(core::Color::Hex(0xFFFFFF)).fillRoundRect(box, 8.0);
p.color(core::Color::Hex(0xE0DED6)).strokeWidth(1.0).strokeRoundRect(box, 8.0);
p.color(kAccent).line({x, y}, {x + w, y});
p.color(kAccent, 0.5).fillCircle(center, 6.0);   // 第二参数是额外透明度
```

`fillRect` / `strokeRect` / `fillRoundRect` / `strokeRoundRect` / `line` / `polyline` /
`fillPolygon` / `fillEllipse` / `strokeEllipse` / `fillCircle` / `strokeCircle` / `clear`。

### 5.3 文字

```cpp
// 启动时加载（路径就是打包进虚拟文件系统的 /public/<文件名>）
auto face = render::FontLibrary::shared().load("public/JetBrainsMono-Light.ttf");
app.text().setDefaultFace(face);

// 画
render::Font font(face, 14.0);                  // 字体面 + 像素字号
render::TextLayoutOptions options;
options.maxWidth = 300.0;
options.wrap = true;
options.maxLines = 2;
options.ellipsis = "...";
const core::RectD used = app.text().drawText("hello", font, {40, 40}, options, kInk);
```

`TextLayoutOptions` 还支持 `align`（左 / 中 / 右）、`lineHeight`、`letterSpacing`。
`drawText` 返回实际占用的矩形，`measure()` 只量不画。

字体面拿不到时（比如没打包这支字体）`Font::builtinFace()` 会兜底成内置点阵字体，页面不会白屏。

### 5.4 场景树与透明度继承

```cpp
core::SceneNode root(core::NodeType::Group, "root");

core::SceneStyle dim;
dim.opacity = 0.45;              // 作用于整棵子树
root.setStyle(dim);

auto label = std::make_unique<core::SceneNode>(core::NodeType::Text, "label");
label->setText("revenue");
label->setFrame({0, 0, 200, 18});
root.appendChild(std::move(label));

app.text().drawScene(root);
```

节点的 `frame` / `points` 都是父坐标系下的值，自身 `transform` 再把它变到父坐标系；
`worldBounds()`、`containsPoint()` 帮你算世界坐标与命中。

### 5.5 交互基类：`scene::Widget`

`Widget` 就是 `SceneNode` 加一套**指针账本**——悬停、按下、抬起、点击、指针捕获，
这些每个交互组件都要写一遍的东西，全部收在这里。`Animation` 和 `Drag` 都继承它，
所以未来的按钮 / 滑块 / 滚动条只写自己那点业务。

```cpp
#include "kiriyama/scene/Widget.h"

class Button : public scene::Widget {
public:
    Button() : Widget("button") { setFrame({0, 0, 120, 32}); }

protected:
    void onUpdate(const scene::FrameContext&) override { /* 每帧，状态已就绪 */ }
    void onPress() override { /* 按下 */ }
    void onRelease(bool inside) override { /* 在内部松开才算点击 */ }
    void onPointerEnter() override { /* 悬停开始 */ }
    void onPointerLeave() override { /* 悬停结束 */ }
};
```

宿主每帧喂一次上下文，整棵子树里的 Widget 会被依次 tick（父先于子）：

```cpp
#include "kiriyama/render/SceneHost.h"   // 把 App 的时钟 / 指针抹平成 FrameContext

root->tickTree(render::frameContext(app));
```

可查询的状态：`pointerInside()`、`hovered()`、`pressed()`、`clicked()`（只在松手那一帧为真）、
`pointerPosition()`、`pointerDelta()`、`hasPointerCapture()`。

**命中区**默认取自身的 `frame()`；`frame` 为空时退化成子树包围盒。
注意 `Group` 的 `localBounds()` 恒为空，所以别拿 `worldBounds()` 直接做命中判定——
那是 `Widget::hitTest()` 存在的理由。

> `scene` 层不 include 任何 SDL 头，指针状态是纯数据，所以这些组件在 node 里就能测。

### 5.6 动画：`scene::Animation`

`Animation` 是 `Widget` 的子类（也就是 `SceneNode`），包住一棵子树，整块一起动。

```cpp
#include "kiriyama/scene/Animation.h"

auto card = std::make_unique<scene::Animation>("card");
card->setDefaultDuration(0.2);
card->appendChild(/* 底 + 字，任意节点 */);

// 四条轨道
card->toTranslation({100, 40});                                   // 位移
card->toOpacity(0.0);                                             // 透明度（作用于子树）
card->toScale({1.1, 1.1});                                        // 缩放
card->byTranslation({0, -6}, {.duration = 0.12});                 // 相对位移
card->toOpacity(1.0, {.duration = 0.45, .delay = 0.15,
                      .ease = math::Ease::OutCubic,
                      .done = [] { /* 自然走完才回调 */ }});

// 每帧推进：挂在树里就交给 tickTree；单独用也可以直接喂 dt（秒），两者等价
card->update(app.deltaTime());
app.text().drawScene(*card);
```

**中断语义**（这是它存在的主要理由）：

1. 同一属性只有一条轨道，再起一条会**替换**旧的，不会两条叠着算同一份位移。
2. 新轨道从**当前值**起步，"淡入到一半改淡出"是接着走的，画面不跳。
3. 重复请求（同目标 + 同时长 + 同曲线）直接忽略。所以可以每帧无脑调
   `toOpacity(0)` 而不必自己记状态——这就是"防止重复动画"。
4. `stop()` 停在当前值、`finish()` 跳到目标值，两者都不触发 `done`；
   只有自然走完的轨道才回调，回调里可以安全地再起新动画。

查询与控制：`animating()`、`animating(property)`、`progress(property)`、`stop(property)`、
`stopAll()`、`finish(property)`、`finishAll()`。

缓动曲线在 `math::Ease`：`Linear / InQuad / OutQuad / InOutQuad / InCubic / OutCubic /
InOutCubic / InSine / OutSine / InOutSine / Smoothstep / Smootherstep / OutBack`。

> 两个注意点：
> - `scale` 以节点**原点**为基准，要绕中心缩放就把内容包一层 Group 并偏移。
> - `Animation` 独占节点的 `scale` / `rotation` 分量（位移仍走 `setTranslation`，与外部改动兼容）。
>   需要叠加外层变换时，在外面再套一个 Group。

### 5.7 拖拽：`scene::Drag`

`Drag` 本身就是一个节点：把要拖的内容塞进去，位置由它自己管。

```cpp
#include "kiriyama/scene/Drag.h"

auto drag = std::make_unique<scene::Drag>("card");
drag->setFrame({0, 0, 180, 48});              // 命中区 / 可拖范围
drag->appendChild(内容);

scene::DragOptions options;
options.axis = scene::DragAxis::Both;                 // Both / X / Y / None
options.bounds = core::RectD{0, 0, 960, 540};         // 父坐标里的活动范围
options.threshold = 3.0;                              // 起拖阈值，防手抖
options.moveNode = true;                              // false 时只报 delta，不动节点
drag->setOptions(options);

drag->onDrag([&](const scene::Drag& d) { /* d.delta() 是父坐标累计位移 */ })
     .onEnd([&](const scene::Drag&) { /* 松手 */ });
```

按下 / 移动 / 抬起由基类自动驱动（`tickTree` 里完成），手动驱动也可以：
`begin()` / `move()` / `end()`，`end()` 返回这次是否真的拖动过，用来区分点击与拖拽。

- 收的是**世界坐标**，内部换算到父坐标系，所以放进带位移 / 缩放的父组里依然跟手。
- 起拖即捕获指针，拖出父容器也不会断在半路；松手自动释放。
- `cancel()` 无条件放弃，不触发 `onEnd`。

**和动画的分工**：`Drag` 只写自己的 `translation`，`Animation` 只写 `style.opacity` 等。
两个组件各管一个节点，就不会抢同一个 transform：

```
Drag(位置) → Animation(透明度) → 内容
```

### 5.8 Tooltip

分两段：状态机在 `core`，绘制在 `render`。

```cpp
kiriyama::core::TooltipState tip;                    // 定时 / 显隐 / 动画曲线
render::TooltipRenderer renderer(app.painter(), app.text());

// 每帧把指针 / 焦点喂进去
tip.hover(hoveredId, anchorRect, nowMs);
tip.focus(focusedId, anchorRect, nowMs);
tip.update(nowMs);

if (tip.visible()) {
    renderer.draw(tip.anchor(), {.title = "text", .body = "detail"},
                  app.canvas(), tip.opacity(nowMs));
}
```

`anchor` 用**目标自己的矩形**（不是指针位置），元素滚动 / 重排时气泡跟着走。
定位（自动翻面、贴边收拢、箭头方向）全在 `core::computeTooltipLayout` 里，可以单独测。

### 5.9 表格：`core::TableState`

表格分两份状态：**数据归你，视图状态归组件**。

```cpp
#include "kiriyama/core/Table.h"

core::TableData data;          // 你自己持有，组件只拿 const 引用
data.columns = {
    {"name",  "名称", 140.0, 64.0, core::CellType::Text},
    {"price", "价格",  96.0, 56.0, core::CellType::Number},
};
data.rows.push_back({"row-1", {core::TableCell("苹果"), core::TableCell(12.5)}});

core::TableState state;
state.attach(data, dataVersion);   // 内容变了就自己把 version +1

// 点表头：asc → desc → 未排序，三态循环
const core::TableChange change = state.cycleSort("price");

// 画的时候按 order 取行，不要自己排
for (int position = 0; position < state.rowCount(); ++position) {
    const core::TableRow* row = state.rowAt(position);
}

// 状态指示
const core::SortDir dir = state.directionOf("price");   // Asc / Desc / None
if (state.canSort("name")) { /* 只有可排序列才给手型 */ }
```

三条必须记住的：

- **排序不改数据，只改 `order[]`**。`order()[i]` 是第 i 个可见行在 `data.rows` 里的下标。
  行身份（`TableRow::key`）因此永远稳定，选中、动画、`SceneDiff` 都不会错位。
- **单元格是类型化的**，不是渲染用的字符串。`CellType::Number` 按 double 比、`Integer` 按 int64
  精确比，否则 `"10" < "9"`。显示格式由列的 `format` 在渲染时生成。
- **空值位置固定**，不随升降序翻转。要挪到前面用 `TableOptions::nullsFirst`。

远程排序（分页后端）把 `options.mode` 设成 `SortMode::Remote`：组件只记住"想按哪列、什么方向"，
`order` 保持恒等，你拿到 `change.sort` 后自己重新取数。

把 `sortable` 设成 `false` 的列会被 `cycleSort` 拒绝（`accepted == false`），
点了不会变状态——**不要在表头上给不可排序的列放手型**。

### 5.10 光标

节点上声明想要的光标，向上继承，不用写任何命中判断：

```cpp
header->mutableStyle().cursor = core::CursorShape::Pointer;   // 可点
cell->mutableStyle().cursor   = core::CursorShape::ResizeH;   // 拖列宽
chip->mutableStyle().cursor   = core::CursorShape::Grab;      // 可拖

// 命中时取值，喂给宿主
const core::CursorShape shape = core::resolveCursor(*hit.node);
app.setCursor(shape);
```

`CursorShape::Inherit`（默认值）表示"问父节点"，一路都没有就退化成 `Default`。
`App::setCursor` 内部缓存 `SDL_Cursor` 句柄，形状没变时不碰 SDL，所以**每帧无脑调**也没开销。

⚠️ 每帧开头先 `app.setCursor(Default)` 归位，再让命中的组件覆盖。否则指针移开之后
手型会黏在那里——SDL 只在 `setCursor` 时改光标，不会自己恢复。

---

## 6. 几个容易踩的坑

- **Esc 会退出程序**。`App::dispatch` 内置了这个行为，业务里别拿 Esc 当快捷键
  （要"取消"请用别的键，或者改 `src/src/App.cc`）。
- **`Color::Hex(0x00000033)` 不是带 alpha 的黑**。这个字面量数值上就是 `0x33`，
  会被当成 6 位色 `#000033`（实心深藏蓝）。要带 alpha 用 `Color::Rgba(0, 0, 0, 0.18)`。
- **PowerShell 脚本必须存成 UTF-8 with BOM**。PowerShell 5.1 会把无 BOM 的文件按 GBK 读，
  注释里的中文会吃掉换行，脚本直接语法错误。
- **不要在 `src/` 里写业务分支**。渲染层只认场景树和样式，业务逻辑放 core 或 app。
- **排序时别把 `dir` 乘到整个比较器上**。`dir == Desc ? -cmp : cmp` 会连空值的排名一起翻转，
  点一次降序整块空行就从表底跳到表头。用 `core::compareCells`，它只对非空值取反。
- **排序回不去的原因通常是"在上一次 `order` 上再排"**。`SortDir::None` 必须从 `data` 重算
  恒等置换，`TableState::cycleSort` 已经这么做了。
