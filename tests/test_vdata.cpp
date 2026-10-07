#include "openbl/formats/keyvalues.hpp"
#include "openbl/formats/vdata.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <set>
#include <string>

using namespace openbl;
using namespace openbl::formats;

namespace {

KvDocument kv(std::string_view text) {
    return parse_keyvalues(text, KvOptions{.escaped_quotes = true});
}

} // namespace

TEST_CASE("vdata: stats and feats") {
    const auto stats = parse_stats(kv(R"(StatData {
        General { }
        Attributes {
            "Name" "Attributes" "InternalName" "Attributes"
            Costs { "New" "1" "Raise" "Current_Rating * 4" }
            Stat { "Name" "Strength" "InternalName" "Strength" "Min" "1" "Max" "5" "Default" "1" }
            Stat { "Name" "Old" "InternalName" "Old" "Disabled" "1" Costs { "Raise" "2" } }
        }
    })"));
    REQUIRE(stats.size() == 1);
    CHECK(stats[0].key == "Attributes");
    CHECK(stats[0].raise_cost == "Current_Rating * 4");
    REQUIRE(stats[0].stats.size() == 2);
    CHECK(stats[0].stats[0].max == 5);
    CHECK(stats[0].stats[1].disabled);
    CHECK(stats[0].stats[1].raise_cost == "2");

    const auto feats = parse_feats(kv(R"(FeatData { Feats { Feat {
        "Name" "Lockpicking" "InternalName" "Intrusion" "MaxValue" "10"
        "Base0" "Dexterity" "Base1" "Security" "PCWeighting" "Normal" } } })"));
    REQUIRE(feats.size() == 1);
    CHECK(feats[0].bases == std::vector<std::string>{"Dexterity", "Security"});
    CHECK(feats[0].max_value == 10);
}

TEST_CASE("vdata: template inheritance") {
    const auto templates = parse_char_templates(kv(R"(ClanDataTables {
        ClanData {
            Text { "TemplateName" "Base" "Name" "Base" }
            General { "Clan" "None" "Kindred" "0" }
            Attributes { "Strength" "1" "Stamina" "2" }
        }
        ClanData {
            Text { "TemplateName" "Cop" "ParentTemplateName" "Base" }
            Attributes { "Strength" "3" }
            Reactions { To { "Brujah" "Hostile" } }
        }
        ClanData {
            Text { "TemplateName" "Loop" "ParentTemplateName" "Loop" }
        }
    })"));
    REQUIRE(templates.size() == 3);
    const auto cop = resolve_char_template(templates, "cop");
    CHECK(cop.template_name == "Cop");
    CHECK(cop.name == "Base");
    CHECK(vdata_field(cop.attributes, "Strength") == "3");
    CHECK(vdata_field(cop.attributes, "Stamina") == "2");
    CHECK(vdata_field(cop.general, "Clan") == "None");
    CHECK(vdata_field(cop.reactions_to, "brujah") == "Hostile");
    CHECK(resolve_char_template(templates, "Loop").template_name == "Loop"); // Cycle cut.
    CHECK(resolve_char_template(templates, "Nobody").template_name.empty());
}

TEST_CASE("vdata: quests, vendors and items") {
    const auto quests = parse_quests(kv(R"(QuestTable { Quest {
        "Title" "Mercurio" "DisplayName" "The Mercurio Job"
        CompletionState { "ID" "1" "Description" "Find him." "Type" "incomplete" }
        CompletionState { "ID" "2" "Description" "Done." "Type" "success" "AwardXP" "1" "AwardMoney" "50" }
    } })"));
    REQUIRE(quests.size() == 1);
    REQUIRE(quests[0].states.size() == 2);
    CHECK(quests[0].states[1].award_money == 50);
    CHECK(quests[0].states[1].type == "success");

    const auto vendors = parse_vendors(kv(R"(VendorData { Vendor { "Name" "Trip" "InternalName" "Trip"
        VendorItem { "Name" "item_w_knife" "InternalName" "item_w_knife" "AmmoOnly" "0" "MinStage" "5" } } })"));
    REQUIRE(vendors.size() == 1);
    REQUIRE(vendors[0].items.size() == 1);
    CHECK(vendors[0].items[0].min_stage == 5);

    const auto items = parse_item_tables(kv(R"(ItemTypeData {
        InventorySections { InventorySection { "InternalName" "None" "IsDisplayed" "0" }
                            InventorySection { "InternalName" "Weapons" } }
        ItemTypes { ItemType { "InternalName" "Gun" "InventorySection" "Weapons" "IsWeapon" "1" } }
        StartingEquipTables { StartingEquip { "InternalName" "Cop" "Money" "20" Items { "Item" "a" "Item" "b" } } }
    })"));
    REQUIRE(items.inventory_sections.size() == 2);
    CHECK_FALSE(items.inventory_sections[0].second);
    CHECK(items.inventory_sections[1].second);
    REQUIRE(items.item_types.size() == 1);
    CHECK(items.item_types[0].is_weapon);
    REQUIRE(items.starting_equipment.size() == 1);
    CHECK(items.starting_equipment[0].items == std::vector<std::string>{"a", "b"});
    CHECK(items.starting_equipment[0].money == 20);
}

TEST_CASE("vdata integration: the rule tables load and cross-reference") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    auto doc = [&](const std::string& path) {
        const auto data = gfs.read(path);
        REQUIRE(data);
        return kv({reinterpret_cast<const char*>(data->data()), data->size()});
    };

    const auto stats = parse_stats(doc("vdata/system/stats.txt"));
    std::size_t stat_count = 0;
    std::string categories;
    for (const auto& c : stats) {
        stat_count += c.stats.size();
        categories += c.key + "(" + std::to_string(c.stats.size()) + ") ";
    }
    const auto feats = parse_feats(doc("vdata/system/feats.txt"));

    std::vector<CharTemplate> templates;
    std::vector<Quest> quests;
    std::vector<DisciplinePower> powers;
    for (const auto& path : gfs.list_files()) {
        if (!path.starts_with("vdata/system/") || !path.ends_with(".txt")) {
            continue;
        }
        const std::string file = path.substr(13);
        if (file.starts_with("clandoc") || file.starts_with("npctemplate")) {
            auto t = parse_char_templates(doc(path));
            templates.insert(templates.end(), t.begin(), t.end());
        } else if (file.starts_with("quests_")) {
            auto q = parse_quests(doc(path));
            quests.insert(quests.end(), q.begin(), q.end());
        } else if (file.starts_with("disciplinetgt_")) {
            auto p = parse_discipline_powers(doc(path));
            powers.insert(powers.end(), p.begin(), p.end());
        }
    }
    // Every parent template exists, and every template resolves.
    std::size_t missing_parents = 0, unnamed = 0;
    for (const auto& t : templates) {
        unnamed += t.template_name.empty();
        if (!t.parent.empty()) {
            missing_parents += resolve_char_template(templates, t.parent).template_name.empty();
        }
    }
    std::size_t states = 0, bad_types = 0, xp = 0;
    for (const auto& q : quests) {
        for (const auto& s : q.states) {
            ++states;
            xp += static_cast<std::size_t>(s.award_xp);
            // Two states write "Success" and "Failure"; the game ignores case.
            bad_types += !iequals(s.type, "success") && !iequals(s.type, "failure") && !iequals(s.type, "incomplete");
        }
    }
    const auto vendors = parse_vendors(doc("vdata/system/vendors.txt"));
    std::size_t vendor_items = 0, unknown_items = 0;
    for (const auto& v : vendors) {
        for (const auto& i : v.items) {
            ++vendor_items;
            unknown_items += !gfs.exists("vdata/items/" + i.internal_name + ".txt");
        }
    }
    const auto leveling = parse_leveling_templates(doc("vdata/system/levelingtemplate_000.txt"));
    const auto histories = parse_histories(doc("vdata/system/histories000.txt"));
    const auto items = parse_item_tables(doc("vdata/system/items.txt"));

    MESSAGE("stats: " << categories << "; " << feats.size() << " feats; " << templates.size() << " templates ("
                      << missing_parents << " missing parents); " << quests.size() << " quests with " << states
                      << " states (" << bad_types << " odd types, " << xp << " XP); " << vendors.size()
                      << " vendors selling " << vendor_items << " items (" << unknown_items << " without vdata); "
                      << powers.size() << " discipline powers; " << leveling.size() << " leveling templates; "
                      << histories.size() << " histories; " << items.item_types.size() << " item types, "
                      << items.starting_equipment.size() << " starting kits");
    CHECK(stat_count == 82);
    CHECK(feats.size() == 23);
    CHECK(templates.size() == 160);
    CHECK(unnamed == 0);
    CHECK(missing_parents == 0);
    CHECK(quests.size() == 62);
    CHECK(states == 334);
    CHECK(bad_types == 0);
    CHECK(vendors.size() == 10);
    CHECK(vendor_items == 127);
    // The cut stake and rocket launchers are still in vendor stock lists.
    CHECK(unknown_items == 7);
    CHECK(powers.size() == 30);
    CHECK(leveling.size() == 15);
    CHECK(histories.size() == 72);
    CHECK(items.item_types.size() == 11);
    CHECK(items.starting_equipment.size() == 21);
}
