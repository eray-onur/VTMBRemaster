#include "openbl/formats/vcd.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <map>

using namespace openbl;
using namespace openbl::formats;

TEST_CASE("VCD scene") {
    const auto scene = parse_vcd(R"(// Choreo version 1
actor "Andrei"
{
  bonerename "Bip01" "Bip02"
  channel "Exp1"
  {
    event expression "Joy"
    {
      time 15.453333 20.720001
      param "dialog"
      param2 "Joy"
      event_ramp
      {
        1.0480 0.9198
        2.0 0.5
      }
    }
    active "0"
  }
  channel "Wave"
  {
    event speak "wave"
    {
      time 0.006667 34.211338
      param "Character\Boss\Andrei\Take2.wav"
      fixedlength
      sequenceduration 2.6
    }
  }
}

fps 60
snap off
)");
    CHECK(scene.diagnostics.empty());
    CHECK(scene.fps == 60);
    CHECK_FALSE(scene.snap);
    REQUIRE(scene.actors.size() == 1);
    const auto& actor = scene.actors[0];
    CHECK(actor.name == "Andrei");
    REQUIRE(actor.bone_renames.size() == 1);
    CHECK(actor.bone_renames[0].second == "Bip02");
    REQUIRE(actor.channels.size() == 2);
    CHECK_FALSE(actor.channels[0].active);
    const auto& joy = actor.channels[0].events.at(0);
    CHECK(joy.type == "expression");
    CHECK(joy.param2 == "Joy");
    CHECK(joy.start == doctest::Approx(15.453333f));
    REQUIRE(joy.ramp.size() == 2);
    CHECK(joy.ramp[1].value == doctest::Approx(0.5f));
    const auto& speak = actor.channels[1].events.at(0);
    CHECK(speak.param == "Character\\Boss\\Andrei\\Take2.wav");
    CHECK(speak.fixed_length);
    CHECK(speak.sequence_duration == doctest::Approx(2.6f));
}

TEST_CASE("VCD damage is reported, not fatal") {
    const auto scene = parse_vcd("actor \"A\" { mystery { x } channel \"C\" { event speak \"w\" { time 1 oops } } }\nfps 30");
    CHECK(scene.fps == 30);
    REQUIRE(scene.actors.size() == 1);
    CHECK(scene.actors[0].channels.size() == 1);
    CHECK(scene.diagnostics.size() == 2);
}

TEST_CASE("VCD integration: every choreography scene in the real install parses") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    std::size_t files = 0, actors = 0, events = 0, diagnostics = 0, missing_sounds = 0;
    std::map<std::string, std::size_t> types;
    for (const auto& path : gfs.list_files()) {
        if (!path.ends_with(".vcd")) {
            continue;
        }
        INFO(path);
        const auto data = gfs.read(path);
        const auto scene = parse_vcd({reinterpret_cast<const char*>(data->data()), data->size()});
        ++files;
        CHECK(scene.diagnostics.empty());
        diagnostics += scene.diagnostics.size();
        actors += scene.actors.size();
        for (const auto& a : scene.actors) {
            for (const auto& c : a.channels) {
                for (const auto& e : c.events) {
                    ++events;
                    ++types[e.type];
                    if (e.type == "speak") {
                        // Scenes name .wav files; most voice lines ship as .mp3.
                        const std::string sound = "sound/" + e.param;
                        const std::string mp3 = sound.substr(0, sound.rfind('.')) + ".mp3";
                        missing_sounds += !gfs.exists(sound) && !gfs.exists(mp3);
                    }
                }
            }
        }
    }
    MESSAGE(files << " scenes, " << actors << " actors, " << events << " events (" << types["speak"] << " speak, "
                  << types["expression"] << " expression, " << types["gesture"] << " gesture, " << types["sequence"]
                  << " sequence); " << missing_sounds << " speak events whose sound is missing");
    CHECK(files == 5300);
    CHECK(diagnostics == 0);
    // Steam release: 42 references to authoring paths or misnumbered lines.
    CHECK(missing_sounds <= 42);
}
