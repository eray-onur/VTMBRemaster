#include "openbl/anim/animation.hpp"

#include "openbl/formats/keyvalues.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace openbl::anim {

using formats::MdlBonePose;
using formats::MdlFile;
using formats::MdlSequence;

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Position of `value` along a grid axis of `cells` entries: lower cell and fraction.
std::pair<std::int32_t, float> grid_position(const formats::MdlPoseParameter* param, float start, float end, float value,
                                             std::int32_t cells) {
    if (cells <= 1 || end == start) {
        return {0, 0.0f};
    }
    const float lo = std::min(start, end), hi = std::max(start, end);
    if (param && param->wrap > 0 && (value < lo || value > hi)) {
        value = lo + std::fmod(std::fmod(value - lo, param->wrap) + param->wrap, param->wrap);
    }
    const float s = std::clamp((value - start) / (end - start), 0.0f, 1.0f) * static_cast<float>(cells - 1);
    const auto i = std::min(static_cast<std::int32_t>(s), cells - 2);
    return {i, s - static_cast<float>(i)};
}

} // namespace

MdlBonePose blend(const MdlBonePose& a, const MdlBonePose& b, float t) {
    MdlBonePose out;
    for (std::size_t k = 0; k < 3; ++k) {
        out.position[k] = a.position[k] + (b.position[k] - a.position[k]) * t;
    }
    const float d = a.rotation[0] * b.rotation[0] + a.rotation[1] * b.rotation[1] + a.rotation[2] * b.rotation[2] +
                    a.rotation[3] * b.rotation[3];
    const float sign = d < 0 ? -1.0f : 1.0f;
    float len = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        out.rotation[k] = a.rotation[k] + (sign * b.rotation[k] - a.rotation[k]) * t;
        len += out.rotation[k] * out.rotation[k];
    }
    len = std::sqrt(len);
    if (len > 0) {
        for (float& c : out.rotation) {
            c /= len;
        }
    }
    return out;
}

BlendSample blend_sample(const MdlFile& file, const MdlSequence& seq, std::span<const float> params) {
    std::array<std::pair<std::int32_t, float>, 2> axis{};
    for (std::size_t k = 0; k < 2; ++k) {
        const std::int32_t p = seq.blend_param[k];
        const bool valid = p >= 0 && static_cast<std::size_t>(p) < file.pose_parameters.size();
        const float value = valid && static_cast<std::size_t>(p) < params.size() ? params[static_cast<std::size_t>(p)] : 0.0f;
        axis[k] = grid_position(valid ? &file.pose_parameters[static_cast<std::size_t>(p)] : nullptr, seq.blend_start[k],
                                seq.blend_end[k], value, seq.grid[k]);
    }
    BlendSample out;
    if (seq.blend_animations.empty()) {
        out.animations[0] = seq.animation;
        out.weights[0] = 1;
        return out;
    }
    float total = 0;
    std::size_t n = 0;
    for (std::int32_t di = 0; di < 2; ++di) {
        for (std::int32_t dj = 0; dj < 2; ++dj) {
            const std::int32_t i = std::min(axis[0].first + di, seq.grid[0] - 1);
            const std::int32_t j = std::min(axis[1].first + dj, seq.grid[1] - 1);
            const float w = (di ? axis[0].second : 1 - axis[0].second) * (dj ? axis[1].second : 1 - axis[1].second);
            const std::int32_t a = seq.blend(i, j);
            if (w <= 0 || a < 0 || static_cast<std::size_t>(a) >= file.animations.size()) {
                continue;
            }
            // Merge repeats of the same cell (grids of size 1 along an axis).
            bool merged = false;
            for (std::size_t m = 0; m < n; ++m) {
                if (out.animations[m] == a) {
                    out.weights[m] += w;
                    merged = true;
                }
            }
            if (!merged) {
                out.animations[n] = a;
                out.weights[n++] = w;
            }
            total += w;
        }
    }
    if (total <= 0) {
        out = {};
        out.animations[0] = seq.animation;
        out.weights[0] = seq.animation >= 0 ? 1.0f : 0.0f;
        return out;
    }
    for (float& w : out.weights) {
        w /= total;
    }
    return out;
}

float sequence_duration(const MdlFile& file, const MdlSequence& seq) {
    if (seq.animation < 0 || static_cast<std::size_t>(seq.animation) >= file.animations.size()) {
        return 0;
    }
    const auto& a = file.animations[static_cast<std::size_t>(seq.animation)];
    return a.frame_count > 1 && a.fps > 0 ? static_cast<float>(a.frame_count - 1) / a.fps : 0.0f;
}

std::vector<MdlBonePose> sample_sequence(const MdlFile& file, std::size_t sequence, float cycle,
                                         std::span<const float> params) {
    const MdlSequence& seq = file.sequences.at(sequence);
    const BlendSample sample = blend_sample(file, seq, params);
    std::vector<MdlBonePose> out;
    float accumulated = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        if (sample.animations[k] < 0 || sample.weights[k] <= 0) {
            continue;
        }
        const auto index = static_cast<std::size_t>(sample.animations[k]);
        const auto& anim = file.animations[index];
        std::vector<MdlBonePose> pose;
        if (anim.frame_count <= 1) {
            pose = file.sample_animation(index, 0);
        } else {
            const float frame = std::clamp(cycle, 0.0f, 1.0f) * static_cast<float>(anim.frame_count - 1);
            const auto f0 = std::min(static_cast<std::uint32_t>(frame), anim.frame_count - 2);
            const float t = frame - static_cast<float>(f0);
            pose = file.sample_animation(index, f0);
            const auto next = file.sample_animation(index, f0 + 1);
            for (std::size_t b = 0; b < pose.size(); ++b) {
                pose[b] = blend(pose[b], next[b], t);
            }
        }
        if (out.empty()) {
            out = std::move(pose);
        } else {
            // Running weighted average: blend toward the new pose by its share so far.
            const float share = sample.weights[k] / (accumulated + sample.weights[k]);
            for (std::size_t b = 0; b < out.size(); ++b) {
                out[b] = blend(out[b], pose[b], share);
            }
        }
        accumulated += sample.weights[k];
    }
    if (out.empty()) {
        out = formats::mdl_bind_pose(file.bones);
    }
    return out;
}

AnimationPlayer::AnimationPlayer(std::vector<formats::MdlBone> bones) : bones_(std::move(bones)) {}

void AnimationPlayer::play(std::shared_ptr<const MdlFile> file, std::size_t sequence, float time, float cycle_offset) {
    if (!file || sequence >= file->sequences.size()) {
        return;
    }
    const float fade = tracks_.empty() ? 0.0f : std::max(file->sequences[sequence].fade_in, 0.0f);
    tracks_.push_back({std::move(file), sequence, time, cycle_offset, fade});
}

void AnimationPlayer::set_pose_parameter(std::string_view name, float value) {
    params_[lower(name)] = value;
}

float AnimationPlayer::track_cycle(const Track& track, float time) const {
    const MdlSequence& seq = track.file->sequences[track.sequence];
    const float duration = sequence_duration(*track.file, seq);
    if (duration <= 0) {
        return 0;
    }
    const float c = track.cycle_offset + (time - track.start) / duration;
    if (seq.flags & formats::sequence_flags::Looping) {
        return c - std::floor(c);
    }
    return std::clamp(c, 0.0f, 1.0f);
}

float AnimationPlayer::cycle(float time) const {
    return tracks_.empty() ? 0.0f : track_cycle(tracks_.back(), time);
}

const std::vector<int>& AnimationPlayer::bone_map(const MdlFile& file) {
    auto [it, inserted] = bone_maps_.try_emplace(&file);
    if (inserted) {
        it->second.assign(bones_.size(), -1);
        for (std::size_t b = 0; b < bones_.size(); ++b) {
            for (std::size_t a = 0; a < file.bones.size(); ++a) {
                if (formats::iequals(file.bones[a].name, bones_[b].name)) {
                    it->second[b] = static_cast<int>(a);
                    break;
                }
            }
        }
    }
    return it->second;
}

std::vector<MdlBonePose> AnimationPlayer::pose(float time) {
    // Drop tracks hidden under a fully faded-in later one.
    for (std::size_t i = tracks_.size(); i-- > 1;) {
        const Track& t = tracks_[i];
        if (t.fade_in <= 0 || time - t.start >= t.fade_in) {
            tracks_.erase(tracks_.begin(), tracks_.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    std::vector<MdlBonePose> out = formats::mdl_bind_pose(bones_);
    bool first = true;
    for (const Track& track : tracks_) {
        const MdlFile& file = *track.file;
        std::vector<float> values(file.pose_parameters.size(), 0.0f);
        for (std::size_t p = 0; p < values.size(); ++p) {
            if (auto it = params_.find(lower(file.pose_parameters[p].name)); it != params_.end()) {
                values[p] = it->second;
            }
        }
        const auto sampled = sample_sequence(file, track.sequence, track_cycle(track, time), values);
        const float weight = first || track.fade_in <= 0 ? 1.0f : std::clamp((time - track.start) / track.fade_in, 0.0f, 1.0f);
        const auto& map = bone_map(file);
        for (std::size_t b = 0; b < bones_.size(); ++b) {
            if (map[b] >= 0) {
                out[b] = blend(out[b], sampled[static_cast<std::size_t>(map[b])], weight);
            }
        }
        first = false;
    }
    return out;
}

} // namespace openbl::anim
