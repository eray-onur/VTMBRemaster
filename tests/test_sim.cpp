#include "openbl/formats/bsp.hpp"
#include "openbl/formats/entity_io.hpp"
#include "openbl/sim/player.hpp"
#include "openbl/sim/triggers.hpp"
#include "openbl/sim/world.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <random>

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

TEST_CASE("Collision: player starts, floors, walls, and agreement with line-of-sight") {
    const auto dir = test::game_dir();
    if (!dir) {
        return;
    }
    vfs::GameFileSystem fs(*dir);
    const formats::Vec3 mins{-16, -16, 0}, maxs{16, 16, 72};
    std::size_t maps = 0, starts = 0, embedded = 0, stuck = 0, floors = 0, walls = 0, compared = 0, missed = 0;
    std::string notes;
    std::mt19937 rng(7);
    for (const std::string& path : fs.list_files()) {
        if (!path.starts_with("maps/") || !path.ends_with(".bsp")) {
            continue;
        }
        const auto bsp = formats::parse_bsp(*fs.read(path));
        World world(bsp);
        world.start();
        ++maps;
        for (const auto& e : bsp.entities) {
            const auto cls = e.find("classname");
            const auto o = e.find("origin");
            if (cls == e.end() || cls->second != "info_player_start" || o == e.end()) {
                continue;
            }
            formats::Vec3 p{};
            std::sscanf(o->second.c_str(), "%f %f %f", &p[0], &p[1], &p[2]);
            ++starts;
            p[2] += 1.0f; // The player spawns a unit above its spawn point, as in Source.
            if (world.trace(p, p, mins, maxs).start_solid) {
                // A few starts sit inside geometry (a cutscene's taxi); lift out of it.
                ++embedded;
                notes += " embedded:" + path;
                int lift = 1;
                while (lift <= 64 && world.trace({p[0], p[1], p[2] + lift}, {p[0], p[1], p[2] + lift}, mins, maxs).start_solid) {
                    ++lift;
                }
                if (lift > 64) {
                    ++stuck;
                    continue;
                }
                p[2] += static_cast<float>(lift);
            }
            const Trace down = world.trace(p, {p[0], p[1], p[2] - 1024}, mins, maxs);
            const bool floor = down.fraction < 1 && down.normal[2] > 0.7f;
            floors += floor;
            if (floor && p[2] - down.end[2] > 128) {
                notes += " high:" + path; // Standing on a static prop (no collision for those yet).
            }
            const Trace across = world.trace(p, {p[0] + 16384, p[1], p[2]}, mins, maxs);
            walls += across.fraction < 1;
        }
        // Point traces against the world: where the leaf-based line test is
        // blocked, the trace is too. (The trace also stops at detail brushes,
        // windows and displacements, which the leaves do not hold.)
        const auto& w = bsp.models[0];
        std::uniform_real_distribution<float> ux(w.mins[0], w.maxs[0]), uy(w.mins[1], w.maxs[1]), uz(w.mins[2], w.maxs[2]);
        for (int i = 0; i < 50; ++i) {
            const formats::Vec3 a{ux(rng), uy(rng), uz(rng)}, b{ux(rng), uy(rng), uz(rng)};
            if ((bsp.leafs[bsp.leaf_at(a)].contents & formats::contents::Solid) ||
                (bsp.leafs[bsp.leaf_at(b)].contents & formats::contents::Solid)) {
                continue; // The void around a map is solid to the leaf test but holds no brushes.
            }
            ++compared;
            if (bsp.segment_blocked(a, b)) {
                const Trace t = world.collision().trace(0, a, b, {0, 0, 0}, {0, 0, 0}, contents::Solid);
                missed += t.fraction == 1 && !t.start_solid;
            }
        }
    }
    CHECK(maps == 101);
    MESSAGE(starts << " player starts: " << embedded << " embedded, " << stuck << " stuck, " << floors << " above a floor, "
                   << walls << " with a wall along +x; " << missed << " of " << compared
                   << " blocked lines missed by the trace;" << notes);
    CHECK(stuck == 0);
    CHECK(floors == starts);
    CHECK(missed == 0);
}

TEST_CASE("Player: standing, falling, jumping, and walking every map without passing into solid") {
    const auto dir = test::game_dir();
    if (!dir) {
        return;
    }
    vfs::GameFileSystem fs(*dir);
    {
        const auto bsp = formats::parse_bsp(*fs.read("maps/sp_tutorial_1.bsp"));
        World world(bsp);
        world.start();
        Player& player = world.spawn_player();
        CHECK(world.find("!player").empty()); // A keyword, not a name...
        world.queue_input("!player", "FireUser1", "", 0, kNoEntity, kNoEntity);
        world.advance(0.1);
        CHECK(world.stats().inputs >= 1); // ...that reaches the player.
        auto run = [&](const UserCommand& cmd, float seconds) {
            for (float t = 0; t < seconds; t += 1.0f / 60.0f) {
                player.move(world, cmd, 1.0f / 60.0f);
            }
        };
        // The start floats 8 units above its floor; the player settles on it.
        const formats::Vec3 spawn = player.feet();
        const Trace floor = world.trace(spawn, {spawn[0], spawn[1], spawn[2] - 256}, player.mins(), player.maxs());
        run({}, 1.0f);
        CHECK(player.on_ground());
        CHECK(player.feet()[2] == doctest::Approx(floor.end[2]).epsilon(0.001));
        const formats::Vec3 start = player.feet();
        // Dropped from 40 units, it lands where it stood.
        REQUIRE(player.teleport(world, {start[0], start[1], start[2] + 40}));
        CHECK_FALSE(player.on_ground());
        run({}, 2.0f);
        CHECK(player.on_ground());
        CHECK(std::abs(player.feet()[2] - start[2]) < 0.5f);
        // A jump rises about 45 units.
        float top = player.feet()[2];
        UserCommand jump;
        jump.jump = true;
        for (int i = 0; i < 90; ++i) {
            player.move(world, jump, 1.0f / 60.0f);
            top = std::max(top, player.feet()[2]);
        }
        CHECK(top - start[2] == doctest::Approx(45).epsilon(0.15));
    }
    std::size_t maps = 0, walks = 0, inside = 0, moved = 0;
    for (const std::string& path : fs.list_files()) {
        if (!path.starts_with("maps/") || !path.ends_with(".bsp")) {
            continue;
        }
        const auto bsp = formats::parse_bsp(*fs.read(path));
        World world(bsp);
        world.start();
        Player& player = world.spawn_player();
        const formats::Vec3 start = player.feet();
        ++maps;
        for (int d = 0; d < 8; ++d) {
            player.teleport(world, start);
            UserCommand cmd;
            cmd.forward = 1;
            cmd.yaw = d * 45.0f;
            for (int i = 0; i < 120; ++i) {
                player.move(world, cmd, 1.0f / 60.0f);
            }
            ++walks;
            const auto p = player.feet();
            moved += std::hypot(p[0] - start[0], p[1] - start[1]) > 16;
            if (world.trace(p, p, player.mins(), player.maxs()).start_solid) {
                ++inside;
                MESSAGE("inside solid after walking: " << path << " yaw " << d * 45 << " at " << p[0] << " " << p[1] << " " << p[2]);
            }
        }
    }
    MESSAGE(maps << " maps, " << walks << " walks of 2 s: " << moved << " moved more than 16 units, " << inside
                 << " ended inside solid");
    CHECK(inside == 0);
}

TEST_CASE("Triggers: the player standing in a trigger_once fires it") {
    const auto dir = test::game_dir();
    if (!dir) {
        return;
    }
    vfs::GameFileSystem fs(*dir);
    std::size_t candidates = 0, fired = 0, kept_out = 0;
    for (const std::string& path : fs.list_files()) {
        if (!path.starts_with("maps/") || !path.ends_with(".bsp")) {
            continue;
        }
        const auto bsp = formats::parse_bsp(*fs.read(path));
        World world(bsp);
        world.start();
        Player& player = world.spawn_player();
        for (EntityId id : world.find("trigger_once")) {
            auto* trigger = dynamic_cast<Trigger*>(world.entity(id));
            const std::string_view model = trigger->value("model");
            if (!trigger->active() || model.size() < 2) {
                continue;
            }
            const auto& m = bsp.models[std::strtoul(std::string(model.substr(1)).c_str(), nullptr, 10)];
            const auto o = trigger->origin();
            const formats::Vec3 centre{o[0] + (m.mins[0] + m.maxs[0]) * 0.5f, o[1] + (m.mins[1] + m.maxs[1]) * 0.5f,
                                       o[2] + m.mins[2]};
            ++candidates;
            // Stand in it (lifted out of the floor if needed), then check touches.
            if (!player.teleport(world, centre)) {
                ++kept_out; // No room for the player there.
                continue;
            }
            world.update_touches();
            fired += world.entity(id)->removed();
        }
    }
    MESSAGE(candidates << " touchable trigger_once: " << fired << " fired with the player in them, " << kept_out
                       << " with no room to stand");
    CHECK(fired >= (candidates - kept_out) * 9 / 10);
}

TEST_CASE("Use: buttons") {
    RecordingScriptHost scripts;
    World world(map_from(R"(
{ "classname" "func_button" "targetname" "b" "wait" "2"
  "OnPressed" ",,,0,-1,pressed()," "OnOut" ",,,0,-1,out()," "OnUseLocked" ",,,0,-1,locked()," }
)"),
                &scripts);
    world.start();
    Entity& button = *world.entity(named(world, "b"));
    CHECK(button.use(world, kNoEntity));
    CHECK(button.use(world, kNoEntity)); // Still in: no second press.
    world.advance(2.1);
    world.queue_input("b", "Lock", "", 0, kNoEntity, kNoEntity);
    world.advance(0.1);
    button.use(world, kNoEntity);
    world.advance(0.1);
    std::vector<std::string> calls;
    for (const auto& c : scripts.calls) {
        calls.push_back(c.statement);
    }
    CHECK(calls == std::vector<std::string>{"pressed()", "out()", "locked()"});
}

TEST_CASE("Use: the player opens a door away from where they stand, and not a locked one") {
    const auto dir = test::game_dir();
    if (!dir) {
        return;
    }
    vfs::GameFileSystem fs(*dir);
    const auto bsp = formats::parse_bsp(*fs.read("maps/sp_tutorial_1.bsp"));
    // tutchopdoord: hinged at (116.5, 579.5), its knob at x = 160; a wall along y = 579.5.
    for (const float side : {-1.0f, 1.0f}) {
        World world(bsp);
        world.start();
        Player& player = world.spawn_player();
        REQUIRE(player.teleport(world, {140, 579.5f + side * 40, 150}));
        for (int i = 0; i < 60; ++i) {
            player.move(world, {}, 1.0f / 60.0f); // Settle on the floor.
        }
        const auto eye = player.eye();
        const formats::Vec3 to{140 - eye[0], 579.5f - eye[1], 150 - eye[2]};
        const float len = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        const EntityId used = world.use(player.id(), eye, {to[0] / len, to[1] / len, to[2] / len});
        REQUIRE(used != kNoEntity);
        world.advance(1.5);
        const EntityId door = named(world, "tutchopdoord");
        const auto t = world.render_transform(door);
        REQUIRE(t);
        const auto moved = t->apply({140, 579.5f, 150}); // A point on the closed door.
        CHECK((moved[1] - 579.5f) * side < -10); // It went to the far side.
    }
    World world(bsp);
    world.start();
    Player& player = world.spawn_player();
    const EntityId locked = named(world, "tutchopdoorc");
    CHECK(world.entity(locked)->use(world, player.id())); // Answers (locked)...
    world.advance(1.5);
    CHECK_FALSE(world.render_transform(locked)); // ...and stays shut.
}

