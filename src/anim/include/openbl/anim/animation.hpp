#pragma once

// Runtime animation of studio models: sampling sequences through their blend
// grids, and a player that fades between sequences. Works in local bone poses
// (formats::MdlBonePose); see docs/ANIMATION.md.

#include "openbl/formats/mdl.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openbl::anim {

// Interpolates position linearly and rotation by normalised lerp along the shorter arc.
formats::MdlBonePose blend(const formats::MdlBonePose& a, const formats::MdlBonePose& b, float t);

// Up to four animations of a sequence's blend grid and their weights
// (bilinear between the two grid cells around each pose parameter's value).
struct BlendSample {
    std::array<std::int32_t, 4> animations{-1, -1, -1, -1};
    std::array<float, 4> weights{};
};

// `params` holds a value per pose parameter of `file` (missing entries count
// as 0). Values outside a parameter's range are clamped, except for looping
// parameters (wrap 360), which wrap into it. Broken grid entries (-1) are
// left out and the remaining weights renormalised.
BlendSample blend_sample(const formats::MdlFile& file, const formats::MdlSequence& sequence, std::span<const float> params);

// Seconds for one pass through a sequence: (frames - 1) / fps of its first animation.
float sequence_duration(const formats::MdlFile& file, const formats::MdlSequence& sequence);

// Local pose of every bone of `file` at `cycle` (0 to 1) of `sequence`,
// interpolating between frames and across the blend grid. Throws
// formats::FormatError on corrupt animation data.
std::vector<formats::MdlBonePose> sample_sequence(const formats::MdlFile& file, std::size_t sequence, float cycle,
                                                  std::span<const float> params);

// Plays sequences on one model, fading from the previous sequence over the
// new one's fade-in time. Sequences may come from the model's include files;
// poses are mapped to the model's bones by name.
class AnimationPlayer {
public:
    explicit AnimationPlayer(std::vector<formats::MdlBone> bones);

    // Starts `sequence` of `file` at `time` (seconds). `cycle_offset` (0-1)
    // starts it part-way, e.g. so crowds do not move in lockstep.
    void play(std::shared_ptr<const formats::MdlFile> file, std::size_t sequence, float time, float cycle_offset = 0);
    // By name (case-insensitive), for the sequences' own files. Unknown names are ignored.
    void set_pose_parameter(std::string_view name, float value);

    bool playing() const { return !tracks_.empty(); }
    // Cycle (0-1) of the current sequence at `time`; finished non-looping sequences stay at 1.
    float cycle(float time) const;

    // Local pose of the model's bones at `time`; the bind pose where nothing animates a bone.
    std::vector<formats::MdlBonePose> pose(float time);

private:
    struct Track {
        std::shared_ptr<const formats::MdlFile> file;
        std::size_t sequence = 0;
        float start = 0;        // Seconds.
        float cycle_offset = 0;
        float fade_in = 0;      // Seconds to reach full weight.
    };
    float track_cycle(const Track& track, float time) const;
    const std::vector<int>& bone_map(const formats::MdlFile& file);

    std::vector<formats::MdlBone> bones_;
    std::vector<Track> tracks_; // Oldest first; the last is the current sequence.
    std::unordered_map<std::string, float> params_; // Lowercase name -> value.
    std::unordered_map<const formats::MdlFile*, std::vector<int>> bone_maps_;
};

} // namespace openbl::anim
