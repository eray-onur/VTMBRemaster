#include "openbl/anim/flex.hpp"

#include "openbl/formats/keyvalues.hpp"

#include <algorithm>

namespace openbl::anim {

using formats::MdlFlexOp;

std::vector<float> default_flex_controllers(const formats::MdlFile& file) {
    std::vector<float> out;
    out.reserve(file.flex_controllers.size());
    for (const auto& c : file.flex_controllers) {
        out.push_back(std::clamp(0.0f, std::min(c.min, c.max), std::max(c.min, c.max)));
    }
    return out;
}

bool set_flex_controller(const formats::MdlFile& file, std::vector<float>& controllers, std::string_view name, float value) {
    for (std::size_t i = 0; i < file.flex_controllers.size() && i < controllers.size(); ++i) {
        const auto& c = file.flex_controllers[i];
        if (formats::iequals(c.name, name)) {
            controllers[i] = std::clamp(value, std::min(c.min, c.max), std::max(c.min, c.max));
            return true;
        }
    }
    return false;
}

std::vector<float> evaluate_flex_rules(const formats::MdlFile& file, std::span<const float> controllers) {
    std::vector<float> weights(file.flex_names.size(), 0.0f);
    std::vector<float> stack;
    for (const auto& rule : file.flex_rules) {
        stack.clear();
        for (const MdlFlexOp& op : rule.ops) {
            switch (op.kind) {
            case MdlFlexOp::Const:
                stack.push_back(op.value);
                break;
            case MdlFlexOp::Controller:
                stack.push_back(op.index >= 0 && static_cast<std::size_t>(op.index) < controllers.size()
                                    ? controllers[static_cast<std::size_t>(op.index)]
                                    : 0.0f);
                break;
            case MdlFlexOp::Flex:
                stack.push_back(op.index >= 0 && static_cast<std::size_t>(op.index) < weights.size()
                                    ? weights[static_cast<std::size_t>(op.index)]
                                    : 0.0f);
                break;
            default: {
                if (stack.size() < 2) {
                    break; // Malformed; the shipped rules never do this.
                }
                const float b = stack.back();
                stack.pop_back();
                float& a = stack.back();
                switch (op.kind) {
                case MdlFlexOp::Add: a += b; break;
                case MdlFlexOp::Sub: a -= b; break;
                case MdlFlexOp::Mul: a *= b; break;
                case MdlFlexOp::Div: a = b != 0 ? a / b : 0.0f; break;
                default: break;
                }
            }
            }
        }
        if (!stack.empty() && rule.flex >= 0 && static_cast<std::size_t>(rule.flex) < weights.size()) {
            weights[static_cast<std::size_t>(rule.flex)] = stack.back();
        }
    }
    return weights;
}

float mesh_flex_scale(const std::array<float, 4>& t, float w) {
    if (w <= t[0] || w >= t[3]) {
        return 0.0f;
    }
    if (w < t[1]) {
        return t[1] > t[0] ? (w - t[0]) / (t[1] - t[0]) : 1.0f;
    }
    if (w <= t[2]) {
        return 1.0f;
    }
    return t[3] > t[2] ? (t[3] - w) / (t[3] - t[2]) : 1.0f;
}

} // namespace openbl::anim
