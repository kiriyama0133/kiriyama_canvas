#include "kiriyama/scene/PieChart.h"

#include <utility>

#include "kiriyama/core/SceneBuilder.h"

namespace kiriyama::scene {

PieChart::PieChart(std::string key) : Widget(std::move(key)) {
    palette_ = core::defaultSeriesPalette();
}

// ---------------------------------------------------------------- 数据

void PieChart::setData(const core::PieChartData* data, std::uint64_t version) {
    data_ = data;
    version_ = version;
    dataDirty_ = true;
}

// ---------------------------------------------------------------- 布局 / 外观

void PieChart::setLayoutOptions(const core::PieLayoutOptions& options) {
    options_ = options;
    dataDirty_ = true;
}

void PieChart::setPalette(std::vector<Color> palette) {
    palette_ = palette.empty() ? core::defaultSeriesPalette() : std::move(palette);
}

// ---------------------------------------------------------------- 查询

std::size_t PieChart::sliceCount() const noexcept {
    // 扇区节点是唯一的孩子；用 dynamic_cast 数，容忍以后往树里塞别的节点。
    std::size_t count = 0;
    for (const auto& child : children()) {
        if (dynamic_cast<const PieSlice*>(child.get()) != nullptr) {
            ++count;
        }
    }
    return count;
}

PieSlice* PieChart::sliceAt(std::size_t index) noexcept {
    std::size_t seen = 0;
    for (const auto& child : children()) {
        if (auto* slice = dynamic_cast<PieSlice*>(child.get())) {
            if (seen == index) {
                return slice;
            }
            ++seen;
        }
    }
    return nullptr;
}

const PieSlice* PieChart::sliceAt(std::size_t index) const noexcept {
    std::size_t seen = 0;
    for (const auto& child : children()) {
        if (const auto* slice = dynamic_cast<const PieSlice*>(child.get())) {
            if (seen == index) {
                return slice;
            }
            ++seen;
        }
    }
    return nullptr;
}

PieSlice* PieChart::hoveredSlice() const noexcept {
    for (const auto& child : children()) {
        if (auto* slice = dynamic_cast<PieSlice*>(child.get())) {
            if (slice->hovered()) {
                return slice;
            }
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------- 重建

void PieChart::onUpdate(const FrameContext&) {
    // 延迟重建：setFrame 可能晚于 setData，也允许运行中改 frame（自适应布局）。
    if (dataDirty_ || !(frame() == lastFrame_)) {
        rebuild();
    }
}

void PieChart::rebuild() {
    dataDirty_ = false;
    lastFrame_ = frame();

    static const core::PieChartData kEmpty;
    layout_ = core::computePieLayout(frame().size(), data_ != nullptr ? *data_ : kEmpty, options_);

    // 数据刷新连状态一起清（hover / 外扩进度），新数据不该继承旧片的动画。
    clearChildren();
    if (!layout_.valid) {
        return;
    }

    const Vec2d origin = frame().origin();
    const Vec2d center{layout_.center.x + origin.x, layout_.center.y + origin.y};

    for (std::size_t i = 0; i < layout_.sectors.size(); ++i) {
        const core::PieSector& sector = layout_.sectors[i];

        auto slice = std::make_unique<PieSlice>("slice." + std::to_string(sector.sliceIndex));
        core::ArcD arc;
        arc.center = center;
        arc.innerRadius = layout_.innerRadius;
        arc.outerRadius = layout_.outerRadius;
        arc.startAngle = sector.startAngle;
        arc.sweepAngle = sector.sweepAngle;
        slice->setArc(arc);

        const std::size_t dataIndex = sector.sliceIndex;
        if (data_ != nullptr && dataIndex < data_->slices.size()) {
            const core::PieSlice& raw = data_->slices[dataIndex];
            slice->setData(raw.label, sector.value, sector.fraction);
        } else {
            slice->setData({}, sector.value, sector.fraction);
        }

        if (!palette_.empty()) {
            slice->setFillColor(palette_[i % palette_.size()]);
        }
        slice->setExplodeDistance(explodeDistance_);

        appendChild(std::move(slice));
    }
}

}  // namespace kiriyama::scene
