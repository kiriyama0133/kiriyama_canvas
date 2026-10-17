#include "kiriyama/core/Axis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "kiriyama/math/Ticks.h"

namespace kiriyama::core {
namespace {

/// 给整数部分插千分位逗号。
void insertGrouping(std::string& text) {
    const std::size_t dot = text.find('.');
    const std::size_t end = dot == std::string::npos ? text.size() : dot;
    const std::size_t start =
        (!text.empty() && (text.front() == '-' || text.front() == '+')) ? 1 : 0;
    if (end - start <= 3) {
        return;
    }

    std::string grouped;
    grouped.reserve(text.size() + (end - start) / 3);
    int digits = 0;
    for (std::size_t i = end; i > start; --i) {
        grouped.push_back(text[i - 1]);
        ++digits;
        if (digits % 3 == 0 && i - 1 > start) {
            grouped.push_back(',');
        }
    }
    std::reverse(grouped.begin(), grouped.end());
    grouped.append(text.substr(end));
    text = std::move(grouped);
}

}  // namespace

// ---------------------------------------------------------------- Axis

Axis::Axis(ScaleType type, RangeD domain, RangeD pixelRange) noexcept
    : type_(type), domain_(domain), pixelRange_(pixelRange) {
    rebuild();
}

void Axis::setType(ScaleType type) noexcept {
    type_ = type;
    rebuild();
}

void Axis::setDomain(RangeD domain) noexcept {
    domain_ = domain;
    rebuild();
}

void Axis::setPixelRange(RangeD pixelRange) noexcept {
    pixelRange_ = pixelRange;
    rebuild();
}

void Axis::rebuild() noexcept {
    linear_ = math::LinearScale(domain_, pixelRange_);
    log_ = math::LogScale(domain_, pixelRange_);
}

double Axis::map(double value) const noexcept {
    return type_ == ScaleType::Log ? log_.map(value) : linear_.map(value);
}

double Axis::mapClamped(double value) const noexcept {
    return map(domain_.clamp(value));
}

double Axis::invert(double pixel) const noexcept {
    return type_ == ScaleType::Log ? log_.invert(pixel) : linear_.invert(pixel);
}

bool Axis::valid() const noexcept {
    if (domain_.degenerate() || pixelRange_.degenerate()) {
        return false;
    }
    return type_ != ScaleType::Log || domain_.minimum() > 0.0;
}

std::vector<double> Axis::tickValues(int targetCount) const {
    return type_ == ScaleType::Log ? logTickValues(domain_, targetCount)
                                   : math::linearTicks(domain_, targetCount);
}

// ---------------------------------------------------------------- 格式化

std::string TickFormat::format(double value, double step) const {
    if (!std::isfinite(value)) {
        return "—";
    }
    if (!(step > 0.0) || !std::isfinite(step)) {
        step = 1.0;
    }

    // 浮点残渣收成 0，避免轴上出现 "-0"。
    if (std::abs(value) <= std::abs(step) * 1e-6) {
        return "0";
    }

    double scaled = value;
    double scaledStep = step;
    const char* suffix = "";
    if (compact) {
        const double magnitude = std::abs(value);
        if (magnitude >= 1e12) {
            scaled = value / 1e12;
            scaledStep = step / 1e12;
            suffix = "T";
        } else if (magnitude >= 1e9) {
            scaled = value / 1e9;
            scaledStep = step / 1e9;
            suffix = "B";
        } else if (magnitude >= 1e6) {
            scaled = value / 1e6;
            scaledStep = step / 1e6;
            suffix = "M";
        } else if (magnitude >= 1e3) {
            scaled = value / 1e3;
            scaledStep = step / 1e3;
            suffix = "k";
        }
    }

    int decimals = maxDecimals;
    if (decimals < 0) {
        decimals = 0;
        double probe = std::abs(scaledStep);
        while (decimals < 6 && std::abs(probe - std::round(probe)) > 1e-6) {
            probe *= 10.0;
            ++decimals;
        }
    }

    char buffer[64];
    if (scaled != 0.0 && std::abs(scaled) < 1e-4) {
        // 极小值用科学计数法，否则会全变成 0.000000。
        std::snprintf(buffer, sizeof(buffer), "%.3g", scaled);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, scaled);
    }

    std::string text{buffer};
    if (text == "-0" || text == "-0.0") {
        text = "0";
    }
    if (grouping && !compact) {
        insertGrouping(text);
    }
    text += suffix;
    return text;
}

// ---------------------------------------------------------------- 对数轴

RangeD niceLogDomain(RangeD range) noexcept {
    if (range.empty() || !(range.maximum() > 0.0)) {
        return RangeD{1.0, 10.0};
    }

    double low = range.minimum();
    if (!(low > 0.0)) {
        low = range.maximum() * 1e-3;  // 0 / 负数：向下补三个数量级
    }
    if (!(low > 0.0)) {
        low = std::numeric_limits<double>::min();
    }

    double lowExponent = std::floor(std::log10(low));
    double highExponent = std::ceil(std::log10(range.maximum()));
    if (highExponent - lowExponent < 1.0) {
        highExponent = lowExponent + 1.0;
    }

    // 防止 pow 溢出，同时保证至少跨一个数量级。
    lowExponent = std::max(lowExponent, -300.0);
    highExponent = std::min(highExponent, 308.0);
    if (highExponent - lowExponent < 1.0) {
        lowExponent = highExponent - 1.0;
    }

    return RangeD{std::pow(10.0, lowExponent), std::pow(10.0, highExponent)};
}

std::vector<double> logTickValues(RangeD domain, int targetCount) {
    std::vector<double> values;
    if (domain.empty() || !(domain.maximum() > 0.0)) {
        return values;
    }

    double low = domain.minimum();
    if (!(low > 0.0)) {
        low = domain.maximum() * 1e-6;
    }
    if (!(low > 0.0)) {
        return values;
    }

    const double lowExponent = std::log10(low);
    const double highExponent = std::log10(domain.maximum());
    if (!(highExponent > lowExponent)) {
        values.push_back(std::pow(10.0, lowExponent));
        return values;
    }

    const int target = std::max(2, targetCount);
    const int stride =
        std::max(1, static_cast<int>(std::ceil((highExponent - lowExponent) / target)));

    // 从 domain 内第一个整十的幂开始，避免刻度落到轴外。
    double exponent = std::ceil(lowExponent / stride) * stride;
    for (; exponent <= highExponent + 1e-9; exponent += stride) {
        values.push_back(std::pow(10.0, exponent));
        if (values.size() > 64) {
            break;
        }
    }

    if (values.empty()) {
        values.push_back(std::pow(10.0, std::round(lowExponent)));
    }
    return values;
}

std::vector<double> evenTickValues(RangeD domain, int intervals) {
    std::vector<double> values;
    if (intervals < 1) {
        intervals = 1;
    }
    if (intervals > 512) {
        intervals = 512;
    }

    const double step = domain.span() / static_cast<double>(intervals);
    if (!(step > 0.0) || !std::isfinite(step)) {
        values.push_back(domain.minimum());
        return values;
    }

    values.reserve(static_cast<std::size_t>(intervals) + 1);
    for (int i = 0; i <= intervals; ++i) {
        // 末位强制取端点，避免累加误差让刻度与网格差半像素。
        values.push_back(i == intervals ? domain.maximum()
                                        : domain.minimum() + step * static_cast<double>(i));
    }
    return values;
}

}  // namespace kiriyama::core
