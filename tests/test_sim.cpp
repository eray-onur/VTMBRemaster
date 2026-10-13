#include "openbl/formats/bsp.hpp"
#include "openbl/formats/entity_io.hpp"
#include "openbl/sim/world.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace openbl;
using namespace openbl::sim;

namespace {

formats::BspFile map_from(std::string_view entities) {
    formats::BspFile bsp;
    bsp.entity_pairs = formats::parse_bsp_entity_pairs(entities);
    return bsp;
}

EntityId named(const World& world, std::string_view name) {
    const auto ids = world.find(name);
    REQUIRE(ids.size() == 1);
    return ids[0];
}

} // namespace

TEST_CASE("Entity outputs: Bloodlines' seven fields") {
    auto o = formats::parse_entity_output("trig_cloud,ChangeNow,,0,-1,,");
    REQUIRE(o);
    CHECK(o->target == "trig_cloud");
    CHECK(o->input == "ChangeNow");
    CHECK(o->parameter.empty());
    CHECK(o->delay == 0.0f);
    CHECK(o->times == -1);
    CHECK(o->python.empty());

    o = formats::parse_entity_output(",,,3,1,YukieFloat(421),");
    REQUIRE(o);
    CHECK(o->input.empty());
    CHECK(o->delay == 3.0f);
    CHECK(o->times == 1);
    CHECK(o->python == "YukieFloat(421)");
    // Commas inside the statement are kept.
    o = formats::parse_entity_output(",,,0,-1,f(1, 2),");
    REQUIRE(o);
    CHECK(o->python == "f(1, 2)");
    // Source's five fields.
    o = formats::parse_entity_output("door,Open,,1.5,1");
    REQUIRE(o);
    CHECK(o->delay == 1.5f);
    CHECK_FALSE(formats::parse_entity_output("door,Open"));
    CHECK_FALSE(formats::parse_entity_output("door,Open,,soon,1"));

    // Repeated output keys survive in the ordered pairs.
    const auto ents = formats::parse_bsp_entity_pairs(R"({ "OnTrigger" "a,Kill,,0,-1,," "OnTrigger" "b,Kill,,0,-1,," })");
    REQUIRE(ents.size() == 1);
    CHECK(ents[0].size() == 2);
}

TEST_CASE("Entity I/O: map load, relays, delays, scripts, lights") {
    RecordingScriptHost scripts;
    World world(map_from(R"(
{ "classname" "worldspawn" }
{ "classname" "logic_auto" "spawnflags" "1" "OnMapLoad" "relay_*,Trigger,,1,-1,," }
{ "classname" "logic_relay" "targetname" "relay_a"
  "OnTrigger" "lamp,TurnOff,,0,-1,,"
  "OnTrigger" "crate,ScriptUnhide,,0.5,1,,"
  "OnTrigger" ",,,2,-1,G.Quest = 2," }
{ "classname" "light" "targetname" "lamp" "style" "32" }
{ "classname" "prop_dynamic" "targetname" "crate" "StartHidden" "1" }
)"),
                &scripts);
    const EntityId crate = named(world, "crate");
    CHECK(world.entity(crate)->hidden());
    CHECK(world.light_style_on(32));
    CHECK(world.stats().generic_classes.at("prop_dynamic") == 1);

    world.start();
    CHECK(world.find("logic_auto").empty()); // Spawnflag 1: removed once fired.
    world.advance(0.9);
    CHECK(world.light_style_on(32)); // The relay's input waits a second.
    world.advance(0.2);
    CHECK_FALSE(world.light_style_on(32));
    CHECK(world.entity(crate)->hidden());
    world.advance(0.5);
    CHECK_FALSE(world.entity(crate)->hidden());
    CHECK(scripts.calls.empty());
    world.advance(2.0);
    REQUIRE(scripts.calls.size() == 1);
    CHECK(scripts.calls[0].statement == "G.Quest = 2");
    CHECK(scripts.calls[0].context.caller == named(world, "relay_a"));
    CHECK(world.stats().unhandled == 0);
}

TEST_CASE("Entity I/O: fire counts, activator, Kill, FireUser") {
    World world(map_from(R"(
{ "classname" "logic_relay" "targetname" "once" "OnTrigger" "!activator,Kill,,0,1,," }
{ "classname" "logic_relay" "targetname" "user" "OnUser2" "once,Trigger,,0,-1,," }
{ "classname" "logic_relay" "targetname" "victim" }
{ "classname" "logic_relay" "targetname" "victim2" }
)"));
    world.start();
    const EntityId victim = named(world, "victim");
    const EntityId victim2 = named(world, "victim2");
    world.queue_input("user", "FireUser2", "", 0, victim, kNoEntity);
    world.advance(0.1);
    CHECK(world.entity(victim)->removed()); // FireUser2 -> OnUser2 -> once -> !activator Kill.
    // The connection fired once and is gone.
    world.queue_input("once", "Trigger", "", 0, victim2, kNoEntity);
    world.advance(0.1);
    CHECK_FALSE(world.entity(victim2)->removed());
    CHECK(world.find("victim").empty());
}

TEST_CASE("Entity I/O: timers and oscillators") {
    RecordingScriptHost scripts;
    World world(map_from(R"(
{ "classname" "logic_timer" "RefireTime" "2" "OnTimer" ",,,0,-1,tick()," }
{ "classname" "logic_timer" "targetname" "osc" "spawnflags" "1" "RefireTime" "1"
  "OnTimerHigh" ",,,0,-1,high()," "OnTimerLow" ",,,0,-1,low()," }
)"),
                &scripts);
    world.start();
    world.advance(4.05);
    std::size_t ticks = 0, highs = 0, lows = 0;
    for (const auto& c : scripts.calls) {
        ticks += c.statement == "tick()";
        highs += c.statement == "high()";
        lows += c.statement == "low()";
    }
    CHECK(ticks == 2);
    CHECK(highs == 2);
    CHECK(lows == 2);
    world.queue_input("osc", "Disable", "", 0, kNoEntity, kNoEntity);
    scripts.calls.clear();
    world.advance(3);
    CHECK(std::none_of(scripts.calls.begin(), scripts.calls.end(), [](const auto& c) { return c.statement != "tick()"; }));
}

TEST_CASE("Entity I/O: sprites and model skins") {
    World world(map_from(R"(
{ "classname" "env_sprite" "targetname" "lit" "spawnflags" "1" }
{ "classname" "env_sprite" "targetname" "dark" }
{ "classname" "prop_dynamic" "targetname" "lamp" "model" "models/a/lamp.mdl" "skin" "1" }
{ "classname" "logic_relay" "targetname" "r" "skin" "3" }
)"));
    world.start();
    const EntityId lit = named(world, "lit"), dark = named(world, "dark"), lamp = named(world, "lamp");
    CHECK_FALSE(world.entity(lit)->hidden()); // "Start on".
    CHECK(world.entity(dark)->hidden());
    CHECK(world.entity(lamp)->skin() == 1);
    CHECK(world.entity(named(world, "r"))->skin() == -1); // Not a model.
    const auto before = world.revision();
    world.queue_input("lit", "HideSprite", "", 0, kNoEntity, kNoEntity);
    world.queue_input("dark", "ToggleSprite", "", 0, kNoEntity, kNoEntity);
    world.queue_input("lamp", "Skin", "2", 0, kNoEntity, kNoEntity);
    world.advance(0.1);
    CHECK(world.entity(lit)->hidden());
    CHECK_FALSE(world.entity(dark)->hidden());
    CHECK(world.entity(lamp)->skin() == 2);
    CHECK(world.revision() > before);
}

TEST_CASE("Movers: a swinging door, its portal, a linked door and a knob it carries") {
    RecordingScriptHost scripts;
    World world(map_from(R"(
{ "classname" "func_door_rotating" "targetname" "door" "origin" "100 0 0" "distance" "90" "speed" "100"
  "wait" "2" "spawnflags" "256" "linked_door" "door2"
  "OnFullyOpen" ",,,0,-1,opened()," "OnFullyClosed" ",,,0,-1,closed()," }
{ "classname" "func_door_rotating" "targetname" "door2" "origin" "300 0 0" "distance" "90" "speed" "100" "wait" "-1" }
{ "classname" "func_areaportal" "portalnumber" "7" "StartOpen" "0" "target" "door" }
{ "classname" "prop_doorknob" "parentname" "door" "origin" "110 0 40" }
)"),
                &scripts);
    world.start();
    CHECK_FALSE(world.portal_open(7));
    const EntityId door = named(world, "door");
    const EntityId knob = world.find("prop_doorknob")[0];
    CHECK_FALSE(world.render_transform(knob));
    world.queue_input("door", "Open", "", 0, kNoEntity, kNoEntity);
    world.advance(0.45);
    CHECK(world.portal_open(7)); // Opens as the door starts to move.
    // About 45 degrees about z through the door's origin: (110, 0) -> (100 + 10 cos 45, 10 sin 45).
    const auto t = world.render_transform(knob);
    REQUIRE(t);
    const auto p = t->apply({110, 0, 40});
    CHECK(p[0] == doctest::Approx(107.1).epsilon(0.02));
    CHECK(p[1] == doctest::Approx(7.1).epsilon(0.02));
    CHECK(p[2] == doctest::Approx(40));
    world.advance(0.6);
    REQUIRE(scripts.calls.size() == 1);
    CHECK(scripts.calls[0].statement == "opened()");
    CHECK(world.render_transform(named(world, "door2"))); // The linked door opened too.
    world.advance(2.0 + 1.0);                               // Waits 2 s, then closes in 0.9 s.
    REQUIRE(scripts.calls.size() == 2);
    CHECK(scripts.calls[1].statement == "closed()");
    CHECK_FALSE(world.render_transform(door));
    CHECK_FALSE(world.portal_open(7));
    world.advance(1.0);
    CHECK_FALSE(world.render_transform(named(world, "door2"))); // Linked: it closes with it.
}

TEST_CASE("Movers: a sliding door and a fan") {
    formats::BspFile bsp = map_from(R"(
{ "classname" "worldspawn" }
{ "classname" "func_door" "targetname" "slide" "model" "*1" "angles" "-90 0 0" "speed" "50" "lip" "8" "wait" "-1" }
{ "classname" "func_rotating" "targetname" "fan" "origin" "0 0 100" "maxspeed" "90" "spawnflags" "1" }
)");
    bsp.models.resize(2);
    bsp.models[1].mins = {0, 0, 0};
    bsp.models[1].maxs = {64, 8, 100}; // Moves up by (100 - 2) - 8 = 90 units.
    World world(bsp);
    world.start();
    world.queue_input("slide", "Open", "", 0, kNoEntity, kNoEntity);
    world.advance(3.0);
    const auto t = world.render_transform(named(world, "slide"));
    REQUIRE(t);
    CHECK(t->translation[0] == doctest::Approx(0).epsilon(0.001));
    CHECK(t->translation[2] == doctest::Approx(90));
    // The fan: 90 degrees a second about z through (0, 0, 100).
    const auto f = world.render_transform(named(world, "fan"));
    REQUIRE(f);
    const auto p = f->apply({10, 0, 100});
    CHECK(std::hypot(p[0], p[1]) == doctest::Approx(10));
    world.queue_input("fan", "Stop", "", 0, kNoEntity, kNoEntity);
    world.advance(0.05);
    const auto stopped = world.render_transform(named(world, "fan"))->apply({10, 0, 100});
    world.advance(1.0);
    CHECK(world.render_transform(named(world, "fan"))->apply({10, 0, 100})[0] == doctest::Approx(stopped[0]));
}

TEST_CASE("Entity I/O: every shipped map runs") {
    const auto dir = test::game_dir();
    if (!dir) {
        return;
    }
    vfs::GameFileSystem fs(*dir);
    std::size_t maps = 0, entities = 0, outputs = 0, inputs = 0, scripts = 0;
    std::map<std::string, std::size_t> unhandled;
    for (const std::string& path : fs.list_files()) {
        if (!path.starts_with("maps/") || !path.ends_with(".bsp")) {
            continue;
        }
        const auto data = fs.read(path);
        REQUIRE(data);
        const auto bsp = formats::parse_bsp(*data);
        CHECK(bsp.entity_pairs.size() == bsp.entities.size());
        World world(bsp);
        world.start();
        for (int step = 0; step < 300; ++step) {
            world.advance(0.1);
        }
        ++maps;
        entities += world.stats().entities;
        outputs += world.stats().outputs;
        inputs += world.stats().inputs;
        scripts += world.stats().scripts;
        for (const auto& [name, n] : world.stats().unhandled_inputs) {
            unhandled[name] += n;
        }
    }
    CHECK(maps == 101);
    CHECK(outputs == 16214);
    std::vector<std::pair<std::size_t, std::string>> top;
    for (const auto& [name, n] : unhandled) {
        top.emplace_back(n, name);
    }
    std::sort(top.rbegin(), top.rend());
    std::cout << "sim: " << maps << " maps, " << entities << " entities, " << outputs << " outputs; in the first 30 s "
              << inputs << " inputs, " << scripts << " scripts; most unhandled:";
    for (std::size_t i = 0; i < std::min<std::size_t>(top.size(), 12); ++i) {
        std::cout << " " << top[i].second << " " << top[i].first;
    }
    std::cout << "\n";
}
