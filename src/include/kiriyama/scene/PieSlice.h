#pragma once

// 扇形组件：TooltipTarget 的第一个"非矩形命中区"子类。
//
// 继承链（饼图策略）：
//   TooltipTarget  —— 气泡内容与开关（与具体形状无关）
//   PieSlice       —— 一个扇区的几何（极坐标命中）+ hover 外扩 + 默认气泡
//
// 命中：Widget 默认用世界 AABB，对扇形来说既误伤（框住邻居）又漏判（框内圆外）。
// 覆盖 Widget::hitShape()，用 core::ArcD 的半径 + 角度判定；甜甜圈内孔不算命中。
// arc 存**父坐标系**坐标（饼图所有扇区共享圆心，直接用布局结果最省事），
// 前提是本节点自身 transform 保持单位——命中时把世界点经 toLocal() 转回来。
//
// hover 外扩：**半径放大，不是整体平移**。扇区是"圆心到外弧"的楔形，
// 沿径向变长时角度边界不动，相邻扇区一像素都不会露出来；
// 早期版本沿角平分线平移整块，hover 时相邻扇区边上会露出一条会动的底色
// （视觉上就是"其他块闪白"）。外扩量是"画"出来的参数（渲染层把外半径加大），
// 不是节点 transform——不归 Animation 管（一个组件只管一个属性）。
// explodeProgress_ 在 onUpdate 里按固定时长线性趋近 hovered?1:0，进出都平滑，
// 渲染层拿 progress 同时控制半径增量与气泡跟随。
//
// 默认气泡：label 作标题，"数值 · 百分比%" 作正文。定制入口按 TooltipTarget
// 的约定：setTooltipContent() 设了静态内容就用静态（入口一），
// 或者继承覆盖 tooltipContent() 按数据生成（本类自己就是这么实现的）。

#include <string>

#include "kiriyama/core/Geometry.h"
#include "kiriyama/scene/TooltipTarget.h"

namespace kiriyama::scene {

using core::ArcD;
using core::Color;
using core::Vec2d;

class PieSlice final : public TooltipTarget {
public:
    explicit PieSlice(std::string key = {});

    // ------------------------------------------------------------ 几何 / 数据

    /// 扇区几何，父坐标系（自身 transform 保持单位）。
    void setArc(const ArcD& arc) noexcept;
    [[nodiscard]] const ArcD& arc() const noexcept { return arc_; }

    /// 数据与标签。fraction 是占比 0..1，气泡和标签都要用。
    void setData(std::string label, double value, double fraction) noexcept;
    [[nodiscard]] const std::string& label() const noexcept { return label_; }
    [[nodiscard]] double value() const noexcept { return value_; }
    [[nodiscard]] double fraction() const noexcept { return fraction_; }

    // ------------------------------------------------------------ 外观

    void setFillColor(Color color) noexcept { fillColor_ = color; }
    [[nodiscard]] Color fillColor() const noexcept { return fillColor_; }

    /// hover 时外半径沿径向放大的像素数。0 关闭外扩（纯 hover 态，只有气泡）。
    void setExplodeDistance(double pixels) noexcept { explodeDistance_ = pixels < 0.0 ? 0.0 : pixels; }
    [[nodiscard]] double explodeDistance() const noexcept { return explodeDistance_; }

    // ------------------------------------------------------------ hover 外扩状态

    /// 0..1，进出 hover 的平滑进度（onUpdate 里推进）。
    [[nodiscard]] double explodeProgress() const noexcept { return explodeProgress_; }

    /// 当前的半径增量（explodeDistance × progress）。渲染层把它加到外半径上。
    [[nodiscard]] double explodeRadius() const noexcept {
        return explodeDistance_ * explodeProgress_;
    }

    // ------------------------------------------------------------ 气泡（TooltipTarget）

    [[nodiscard]] TooltipText tooltipContent() const override;
    [[nodiscard]] bool hasTooltipContent() const override;
    /// 锚在扇区中径点上（不是 AABB 中心）——气泡贴着扇区，不飘到外接框角落。
    [[nodiscard]] core::RectD tooltipAnchor() const override;

protected:
    /// 极坐标命中：半径在 [inner, outer]，角度落在扇区内。
    [[nodiscard]] bool hitShape(const Vec2d& worldPoint) const noexcept override;

    /// 推进外扩进度。
    void onUpdate(const FrameContext& ctx) override;

private:
    ArcD arc_{};
    std::string label_{};
    double value_{0.0};
    double fraction_{0.0};

    Color fillColor_{Color::FromBytes(0x85, 0xB7, 0xEB)};
    Color borderColor_{Color::FromBytes(0x2C, 0x2C, 0x2A)};
    double explodeDistance_{6.0};
    double borderWidth_{0.0};

    double explodeProgress_{0.0};
};

}  // namespace kiriyama::scene
