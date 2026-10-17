#include "kiriyama/core/NodeType.h"

namespace kiriyama::core {

const char* toString(NodeType type) noexcept {
    switch (type) {
        case NodeType::Group:
            return "group";
        case NodeType::Rect:
            return "rect";
        case NodeType::Ellipse:
            return "ellipse";
        case NodeType::Line:
            return "line";
        case NodeType::Polyline:
            return "polyline";
        case NodeType::Polygon:
            return "polygon";
        case NodeType::Text:
            return "text";
    }
    return "unknown";
}

}  // namespace kiriyama::core
