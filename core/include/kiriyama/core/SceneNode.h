#pragma once

// 场景节点：core 维护的抽象图形树。
//
// 一条树同时承担三件事：
//   - 描述  每个节点是一种图元（矩形 / 椭圆 / 线 / 折线 / 多边形 / 文本）+ 样式 + 局部变换
//   - 查询  按 id / key / 谓词遍历，算局部 / 世界包围盒，做命中测试
//   - 复用  reconcileScene() 把"新一帧的描述树"对接到这棵树上，只更新变化的节点
//
// 节点不持有像素、不持有字体、不直接画。渲染层拿着树去遍历即可，
// 因此同一棵树既能喂给 SDL，也能喂给 wasm 侧的 canvas。
//
// 坐标：节点的 frame / points 都是**父节点坐标系**下的值，自身 transform 再把它变到父坐标系。
// 世界坐标由整条父链相乘得到（见 worldTransform）。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Geometry.h"
#include "NodeType.h"
#include "SceneStyle.h"
#include "kiriyama/math/Transform2D.h"

namespace kiriyama::core {

using math::Affine2D;

class SceneNode {
public:
    explicit SceneNode(NodeType type = NodeType::Group, std::string key = {});
    // 析构必须是虚的：子类（scene::Widget 及其派生）会通过 unique_ptr<SceneNode> 持有，
    // 非虚析构会让派生类成员不被析构。顺便让 SceneNode 成为多态类型，
    // 树遍历里的 dynamic_cast<Widget*> 才有得可转。
    virtual ~SceneNode();

    // 节点有父指针，拷贝 / 移动都没有合理语义；要复制请用 clone()。
    SceneNode(const SceneNode&) = delete;
    SceneNode& operator=(const SceneNode&) = delete;
    SceneNode(SceneNode&&) = delete;
    SceneNode& operator=(SceneNode&&) = delete;

    /// 进程内唯一，构造时分配。用于 diff 时判断"还是不是同一个节点"。
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }

    [[nodiscard]] NodeType type() const noexcept { return type_; }
    void setType(NodeType type) noexcept;

    /// 稳定业务标识，diff 用它配对前后两个节点（类比 Vue 的 key）。
    [[nodiscard]] const std::string& key() const noexcept { return key_; }
    void setKey(std::string key);

    /// 调试 / 查询用的标签，不参与 diff 判定。
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void setVisible(bool visible) noexcept;

    // ------------------------------------------------------------ 图元数据

    /// 局部坐标下的排版盒：Rect / Ellipse / Text 用。
    [[nodiscard]] const RectD& frame() const noexcept { return frame_; }
    void setFrame(const RectD& frame) noexcept;

    /// Line / Polyline / Polygon 的顶点，父坐标系。
    [[nodiscard]] std::span<const Vec2d> points() const noexcept { return points_; }
    void setPoints(std::vector<Vec2d> points);
    void addPoint(const Vec2d& point);
    [[nodiscard]] std::vector<Vec2d> takePoints() noexcept { return std::move(points_); }

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    void setText(std::string text);
    [[nodiscard]] std::string takeText() noexcept { return std::move(text_); }

    /// 子树裁剪矩形（**本节点局部坐标**）。empty = 不裁剪。
    /// 描述树用它表达"滚动视口只露出一部分"这类语义，渲染层落成平台裁剪。
    [[nodiscard]] const RectD& clipRect() const noexcept { return clipRect_; }
    void setClipRect(const RectD& rect) noexcept;

    [[nodiscard]] const SceneStyle& style() const noexcept { return style_; }
    void setStyle(const SceneStyle& style) noexcept;
    [[nodiscard]] SceneStyle& mutableStyle() noexcept;  ///< 直接改字段；调用方负责 markDirty

    [[nodiscard]] const Affine2D& transform() const noexcept { return transform_; }
    void setTransform(const Affine2D& transform) noexcept;
    void setTranslation(const Vec2d& offset) noexcept;
    void translate(const Vec2d& delta) noexcept;

    // ------------------------------------------------------------ 树结构

    [[nodiscard]] SceneNode* parent() noexcept { return parent_; }
    [[nodiscard]] const SceneNode* parent() const noexcept { return parent_; }

    [[nodiscard]] std::size_t childCount() const noexcept { return children_.size(); }
    [[nodiscard]] SceneNode* childAt(std::size_t index) noexcept;
    [[nodiscard]] const SceneNode* childAt(std::size_t index) const noexcept;
    [[nodiscard]] std::span<const std::unique_ptr<SceneNode>> children() const noexcept {
        return children_;
    }

    [[nodiscard]] int indexInParent() const noexcept;

    /// 追加子节点，返回其裸指针（所有权仍在父节点）。
    SceneNode* appendChild(std::unique_ptr<SceneNode> child);
    SceneNode* insertChild(std::size_t index, std::unique_ptr<SceneNode> child);

    /// 移除第 index 个子节点并返回所有权；越界返回 nullptr。
    std::unique_ptr<SceneNode> removeChild(std::size_t index);
    /// 移除指定子节点并返回所有权；不是直接子节点时返回 nullptr。
    std::unique_ptr<SceneNode> removeChild(SceneNode* child);
    void clearChildren();

    /// 把子节点整体搬走（父指针保持指向本节点，方便原样接回）。
    /// diff 重排子节点时用它，避免逐个 detach 的 O(n²)。
    /// 注意：调用后 children 为空，必须紧接着 setChildren() 把列表接回来。
    std::vector<std::unique_ptr<SceneNode>> takeChildren();

    /// 整体替换子节点，统一设置 parent。旧节点直接销毁。
    /// structurallyChanged 为 false 时不标脏——diff 在"结构没变"的路径上用它，
    /// 避免每帧把整棵树标脏、让增量重绘失效。
    void setChildren(std::vector<std::unique_ptr<SceneNode>> children,
                     bool structurallyChanged = true);

    /// 深拷贝：结构 / 属性 / key 全保留，id 重新分配（id 是身份，不是内容）。
    [[nodiscard]] std::unique_ptr<SceneNode> clone() const;

    // ------------------------------------------------------------ 几何

    /// 自身图元在局部坐标下的包围盒（不含子节点）。
    [[nodiscard]] RectD localBounds() const noexcept;

    /// 自身 + 子树在**本节点坐标系**下的包围盒。
    [[nodiscard]] RectD bounds() const noexcept;

    [[nodiscard]] Affine2D worldTransform() const noexcept;

    /// clipRect 变换到世界坐标（四角取包围盒）。未设置裁剪时返回 empty。
    [[nodiscard]] RectD worldClipRect() const noexcept;

    /// 世界坐标包围盒（含子树）。
    [[nodiscard]] RectD worldBounds() const noexcept;

    [[nodiscard]] Vec2d toWorld(const Vec2d& localPoint) const noexcept;
    [[nodiscard]] Vec2d toLocal(const Vec2d& worldPoint) const noexcept;

    /// 局部坐标命中自身图元。tolerance 让细线 / 小点更好点中。
    [[nodiscard]] bool containsLocal(const Vec2d& localPoint, double tolerance = 0.0) const noexcept;

    /// 世界坐标命中自身图元（不含子节点）。
    [[nodiscard]] bool containsPoint(const Vec2d& worldPoint, double tolerance = 0.0) const noexcept;

    // ------------------------------------------------------------ 脏标记

    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void markDirty() noexcept { dirty_ = true; }
    void markSubtreeDirty() noexcept;
    void clearDirty() noexcept { dirty_ = false; }
    void clearSubtreeDirty() noexcept;

    /// 子树里（含自身）是否有脏节点——渲染层靠它整棵跳过。
    [[nodiscard]] bool hasDirtyInSubtree() const noexcept;

private:
    std::uint64_t id_{0};
    NodeType type_{NodeType::Group};
    std::string key_;
    std::string name_;
    bool visible_{true};
    bool dirty_{true};

    RectD frame_{};
    RectD clipRect_{};
    std::vector<Vec2d> points_;
    std::string text_;

    SceneStyle style_{};
    Affine2D transform_{};

    SceneNode* parent_{nullptr};
    std::vector<std::unique_ptr<SceneNode>> children_;
};

}  // namespace kiriyama::core
