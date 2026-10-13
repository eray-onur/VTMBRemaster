#include "openbl/formats/bsp.hpp"
#include "openbl/formats/entity_io.hpp"
#include "openbl/sim/world.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <algorithm>
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
