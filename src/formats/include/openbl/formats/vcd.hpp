#pragma once

// Choreography scenes (.vcd): timed speech, expressions, gestures and
// sequences for one or more actors. See docs/formats/choreography.md.

#include "openbl/formats/text_diagnostic.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openbl::formats {

struct VcdRampPoint {
    float time = 0;  // Seconds from the event's start.
    float value = 0; // 0-1 intensity.
};

struct VcdEvent {
    std::string type; // speak, loud, silence, expression, gesture, sequence, firetrigger, python, bodysound.
    std::string name;
    float start = 0, end = 0; // Seconds from the start of the scene.
    std::string param;        // Sound, expression set, gesture or sequence name, depending on type.
    std::string param2;
    bool fixed_length = false;
    std::optional<float> sequence_duration;
    std::vector<VcdRampPoint> ramp;
};

struct VcdChannel {
    std::string name;
    bool active = true;
    std::vector<VcdEvent> events;
};

struct VcdActor {
    std::string name;
    bool active = true;
    std::string faceposer_model; // Authoring path; one shipped file.
    std::vector<std::pair<std::string, std::string>> bone_renames;
    std::vector<VcdChannel> channels;
};

struct VcdScene {
    std::vector<VcdActor> actors;
    int fps = 0;
    bool snap = false;
    std::vector<TextDiagnostic> diagnostics;
};

// Text is treated as bytes (Windows-1252). Never throws.
VcdScene parse_vcd(std::string_view text);

} // namespace openbl::formats
