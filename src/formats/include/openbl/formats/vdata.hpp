#pragma once

// Typed views of the vdata/system rule tables: stats, feats, clan and NPC
// templates, quests, vendors, discipline powers, leveling templates, histories
// and item tables. See docs/formats/text_data.md.
//
// Each parse_* function takes the file's KeyValues document (parsed with
// KvOptions::escaped_quotes) and never throws. Values the game evaluates are
// kept as strings (e.g. the cost formula "Current_Rating * 4"); fields that
// are always numbers are converted, with 0 for anything unparseable.

#include "openbl/formats/keyvalues.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openbl::formats {

// The leaf key/value pairs of one block, in file order.
using VdataFields = std::vector<std::pair<std::string, std::string>>;

// First value for `key` (case-insensitive), or empty.
std::string_view vdata_field(const VdataFields& fields, std::string_view key);

// --- stats.txt ---------------------------------------------------------------

struct StatDef {
    std::string name, internal_name, help_text;
    std::string name_mapping; // String table used to display values, if any.
    std::string raise_cost;   // Formula; empty to use the category's.
    int min = 0, max = 0, default_value = 0;
    bool disabled = false;
};

struct StatCategory {
    std::string key; // Block name: Attributes, Abilities, Disciplines, ActiveDisciplines, ...
    std::string name, internal_name;
    std::string new_cost, raise_cost; // Formulas.
    std::vector<StatDef> stats;
};
std::vector<StatCategory> parse_stats(const KvDocument& doc);

// --- feats.txt ---------------------------------------------------------------

struct FeatDef {
    std::string name, internal_name, help_text;
    std::vector<std::string> bases; // Stats the feat is derived from (Base0, Base1, ...).
    std::string pc_weighting, npc_weighting; // Dice-roll tables.
    int max_value = 0;
    bool disabled = false;
};
std::vector<FeatDef> parse_feats(const KvDocument& doc);

// --- clandoc000.txt, npctemplate*.txt ----------------------------------------

// A clan or NPC stat template ("ClanData").
struct CharTemplate {
    std::string template_name, parent; // parent: ParentTemplateName, or empty.
    std::string name, description;
    VdataFields general, attributes, abilities, disciplines, numina, resistances;
    VdataFields reactions_to;     // Reactions/To: clan -> reaction.
    VdataFields loiter_activities; // Activity -> weight.
};
std::vector<CharTemplate> parse_char_templates(const KvDocument& doc);

// The template merged over its ParentTemplateName chain: a child's fields
// replace its parent's with the same key. Unknown parents end the chain; a
// cycle is cut where it repeats. Returns an empty template if `name` is unknown.
CharTemplate resolve_char_template(const std::vector<CharTemplate>& templates, std::string_view name);

// --- quests_*.txt ------------------------------------------------------------

struct QuestState {
    int id = 0;
    std::string description;
    std::string type; // success, failure or incomplete.
    int award_xp = 0, award_money = 0;
    std::string event; // Script run on reaching the state, if any.
};

struct Quest {
    std::string title;        // Used by dialogue and scripts.
    std::string display_name; // Journal heading.
    std::vector<QuestState> states;
};
std::vector<Quest> parse_quests(const KvDocument& doc);

// --- vendors.txt -------------------------------------------------------------

struct VendorItem {
    std::string name, internal_name; // An item class, e.g. item_w_knife.
    bool ammo_only = false;
    int min_stage = 0;        // Plot stage from which the item is sold.
    int min_vendor_state = 0;
};

struct Vendor {
    std::string name, internal_name, image;
    std::vector<VendorItem> items;
};
std::vector<Vendor> parse_vendors(const KvDocument& doc);

// --- disciplinetgt_*.txt -----------------------------------------------------

// A targeted discipline power. Its effect blocks (Hit_Human, Hit_Strata_N,
// AoE, Effect_Src_Instant, ...) are kept as KeyValues in `block`.
struct DisciplinePower {
    std::string name, internal_name, discipline;
    int level = 0;
    std::string blood_cost, recovery_time; // Numbers as written.
    bool overt = false;
    KvNode block;
};
std::vector<DisciplinePower> parse_discipline_powers(const KvDocument& doc);

// --- levelingtemplate_000.txt ------------------------------------------------

struct LevelingTemplate {
    std::string name, internal_name, clan_dependency;
    std::vector<VdataFields> levels; // Stat -> value at each level.
    struct Group {
        std::string dependency;
        std::vector<VdataFields> levels;
    };
    std::vector<Group> level_groups;
};
std::vector<LevelingTemplate> parse_leveling_templates(const KvDocument& doc);

// --- histories000.txt --------------------------------------------------------

struct HistoryDef {
    std::string internal_name, name, description, short_description, short_penalty_description;
    std::string effect;             // Trait effect applied.
    std::string critter_scope, genders_disallowed;
};
// HistoryData blocks only; UNUSED_HistoryData is skipped.
std::vector<HistoryDef> parse_histories(const KvDocument& doc);

// --- items.txt ---------------------------------------------------------------

struct ItemTypeDef {
    std::string name, internal_name, inventory_section;
    bool is_weapon = false, is_wielded = false, is_worn = false;
};

struct StartingEquipment {
    std::string name, internal_name;
    int money = 0;
    std::vector<std::string> items;
};

struct ItemTables {
    std::vector<std::pair<std::string, bool>> inventory_sections; // Internal name, displayed.
    std::vector<ItemTypeDef> item_types;
    std::vector<StartingEquipment> starting_equipment;
    std::vector<VdataFields> botch_types;
    std::vector<VdataFields> activation_types;
};
ItemTables parse_item_tables(const KvDocument& doc);

} // namespace openbl::formats
