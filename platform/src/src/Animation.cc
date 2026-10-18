#include "kiriyama/scene/Animation.h"

#include <algorithm>
#include <utility>

#include "kiriyama/math/MathUtils.h"

namespace kiriyama::scene {

namespace {

/// 两个属性值是否"算同一个值"。用来判定重复请求 / 是否需要插值。
[[nodiscard]] bool sameValue(const Vec2d& a, const Vec2d& b) noexcept {
    return math::nearlyEqual(a.x, b.x) && math::nearlyEqual(a.y, b.y);
}

[[nodiscard]] Vec2d lerpVec(const Vec2d& a, const Vec2d& b, double t) noexcept {
    return Vec2d{math::lerp(a.x, b.x, t), math::lerp(a.y, b.y, t)};
}

}  // namespace

Animation::Animation(std::string key) : Widget(std::move(key)) {}

void Animation::setDefaultDuration(double seconds) noexcept {
    defaultDuration_ = seconds > 0.0 ? seconds : 0.0;
}

// ---------------------------------------------------------------- 起动画

Animation& Animation::toTranslation(const Vec2d& target, AnimSpec spec) {
    start(AnimProperty::Translation, target, spec);
    return *this;
}

Animation& Animation::byTranslation(const Vec2d& delta, AnimSpec spec) {
    const Vec2d from = current(AnimProperty::Translation);
    start(AnimProperty::Translation, Vec2d{from.x + delta.x, from.y + delta.y}, spec);
    return *this;
}

Animation& Animation::toOpacity(double target, AnimSpec spec) {
    start(AnimProperty::Opacity, Vec2d{target, 0.0}, spec);
    return *this;
}

Animation& Animation::toScale(const Vec2d& target, AnimSpec spec) {
    start(AnimProperty::Scale, target, spec);
    return *this;
}

Animation& Animation::toRotation(double radians, AnimSpec spec) {
    start(AnimProperty::Rotation, Vec2d{radians, 0.0}, spec);
    return *this;
}

void Animation::start(AnimProperty property, const Vec2d& to, const AnimSpec& spec) {
    const double duration = spec.duration >= 0.0 ? spec.duration : defaultDuration_;
    const Vec2d from = current(property);

    // ---- 中断 1：重复请求直接丢掉。
    // 同目标、同时长、同曲线、没延迟——起不起都一样，重起只会把动画打回第一帧。
    if (const Track* live = find(property)) {
        if (sameValue(live->to, to) && math::nearlyEqual(live->duration, duration) &&
            live->ease == spec.ease && math::nearlyZero(live->delay) &&
            math::nearlyZero(spec.delay)) {
            return;
        }
        // ---- 中断 2：换了目标，替换旧轨道。新轨道从当前值起步，画面连续。
        stop(property);
    }

    // ---- 没有可插值的区间：直接到位，一次 no-op。
    // 值本来就等于目标时连 done 都不触发，避免调用方每帧请求时把回调刷屏。
    if (duration <= 0.0 || sameValue(from, to)) {
        apply(property, to);
        if (spec.done && !sameValue(from, to)) {
            spec.done();
        }
        return;
    }

    Track track;
    track.property = property;
    track.from = from;
    track.to = to;
    track.delay = std::max(0.0, spec.delay);
    track.duration = duration;
    track.ease = spec.ease;
    track.done = std::move(spec.done);
    tracks_.push_back(std::move(track));
}

// ---------------------------------------------------------------- 查询

Animation::Track* Animation::find(AnimProperty property) noexcept {
    for (Track& track : tracks_) {
        if (track.property == property) {
            return &track;
        }
    }
    return nullptr;
}

const Animation::Track* Animation::find(AnimProperty property) const noexcept {
    for (const Track& track : tracks_) {
        if (track.property == property) {
            return &track;
        }
    }
    return nullptr;
}

bool Animation::animating(AnimProperty property) const noexcept {
    return find(property) != nullptr;
}

double Animation::progress(AnimProperty property) const noexcept {
    const Track* track = find(property);
    if (track == nullptr) {
        return 1.0;
    }
    if (track->duration <= 0.0) {
        return 1.0;
    }
    return math::saturate((track->elapsed - track->delay) / track->duration);
}

// ---------------------------------------------------------------- 控制

void Animation::stop(AnimProperty property) noexcept {
    std::erase_if(tracks_, [property](const Track& track) { return track.property == property; });
}

void Animation::stopAll() noexcept {
    tracks_.clear();
}

void Animation::finish(AnimProperty property) noexcept {
    const Track* track = find(property);
    if (track == nullptr) {
        return;
    }
    const Vec2d target = track->to;
    stop(property);
    apply(property, target);
}

void Animation::finishAll() noexcept {
    // 先抄一份目标值：apply 会改 transform / style，期间 tracks_ 不能再被查。
    std::vector<std::pair<AnimProperty, Vec2d>> targets;
    targets.reserve(tracks_.size());
    for (const Track& track : tracks_) {
        targets.emplace_back(track.property, track.to);
    }
    tracks_.clear();
    for (const auto& [property, value] : targets) {
        apply(property, value);
    }
}

// ---------------------------------------------------------------- 推进

bool Animation::update(double deltaSeconds) {
    if (tracks_.empty() || !(deltaSeconds > 0.0)) {
        return false;
    }

    bool changed = false;
    // 回调要等这一轮遍历结束再触发：回调里很可能又起一条新轨道，
    // 在遍历中 push_back 会让 vector 重分配、迭代器当场失效。
    std::vector<std::function<void()>> finished;

    for (auto it = tracks_.begin(); it != tracks_.end();) {
        it->elapsed += deltaSeconds;

        const double local = it->elapsed - it->delay;
        if (local < 0.0) {
            ++it;  // 还在延迟里，属性原样不动
            continue;
        }

        if (local >= it->duration) {
            const Vec2d target = it->to;
            if (it->done) {
                finished.push_back(std::move(it->done));
            }
            const AnimProperty property = it->property;
            it = tracks_.erase(it);
            apply(property, target);
            changed = true;
            continue;
        }

        const double eased = math::ease(it->ease, local / it->duration);
        apply(it->property, lerpVec(it->from, it->to, eased));
        changed = true;
        ++it;
    }

    for (auto& callback : finished) {
        callback();
    }
    return changed;
}

// ---------------------------------------------------------------- 落地

void Animation::apply(AnimProperty property, const Vec2d& value) {
    switch (property) {
        case AnimProperty::Translation:
            // 只改平移分量，旋转 / 缩放由本节点自己维护，不会被这里抹掉。
            setTranslation(value);
            break;

        case AnimProperty::Opacity: {
            const double opacity = math::saturate(value.x);
            if (!math::nearlyEqual(style().opacity, opacity)) {
                mutableStyle().opacity = opacity;
            }
            break;
        }

        case AnimProperty::Scale:
            if (!sameValue(scale_, value)) {
                scale_ = value;
                applyTrs();
            }
            break;

        case AnimProperty::Rotation:
            if (!math::nearlyEqual(rotation_, value.x)) {
                rotation_ = value.x;
                applyTrs();
            }
            break;
    }
}

void Animation::applyTrs() {
    const Affine2D& transform = this->transform();
    setTransform(Affine2D::Trs(Vec2d{transform.m02, transform.m12}, rotation_, scale_));
}

Vec2d Animation::current(AnimProperty property) const noexcept {
    switch (property) {
        case AnimProperty::Translation: {
            const Affine2D& transform = this->transform();
            return Vec2d{transform.m02, transform.m12};
        }
        case AnimProperty::Opacity:
            return Vec2d{style().opacity, 0.0};
        case AnimProperty::Scale:
            return scale_;
        case AnimProperty::Rotation:
            return Vec2d{rotation_, 0.0};
    }
    return Vec2d{};
}

}  // namespace kiriyama::scene
