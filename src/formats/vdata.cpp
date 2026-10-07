#include "openbl/formats/vdata.hpp"

#include <charconv>
#include <cstdlib>
#include <set>

namespace openbl::formats {
namespace {

std::string text(const KvNode& block, std::string_view key) {
    const KvNode* n = block.find(key);
    return n && !n->is_block ? n->value : std::string();
}

// Integer value; tolerates surrounding spaces and a fractional part ("3.0").
int number(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    int v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return ec == std::errc() ? v : 0;
}

int number(const KvNode& block, std::string_view key) {
    return number(text(block, key));
}

bool flag(const KvNode& block, std::string_view key) {
    return number(block, key) != 0;
}

VdataFields fields(const KvNode* block) {
    VdataFields out;
    if (block) {
        for (const KvNode& n : block->children) {
            if (!n.is_block) {
                out.emplace_back(n.key, n.value);
            }
        }
    }
    return out;
}

// Direct child blocks named `key`.
template <typename Fn> void each_block(const KvNode& parent, std::string_view key, Fn&& fn) {
    for (const KvNode& n : parent.children) {
        if (n.is_block && iequals(n.key, key)) {
            fn(n);
        }
    }
}

template <typename Fn> void each_root(const KvDocument& doc, std::string_view key, Fn&& fn) {
    for (const KvNode& n : doc.roots) {
        if (n.is_block && iequals(n.key, key)) {
            fn(n);
        }
    }
}

// Numbered keys Base0, Base1, ... in order.
std::vector<std::string> numbered(const KvNode& block, std::string_view prefix) {
    std::vector<std::string> out;
    for (int i = 0;; ++i) {
        const std::string v = text(block, std::string(prefix) + std::to_string(i));
        if (v.empty()) {
            return out;
        }
        out.push_back(v);
    }
}

void merge(VdataFields& base, const VdataFields& over) {
    for (const auto& [k, v] : over) {
        bool replaced = false;
        for (auto& [bk, bv] : base) {
            if (iequals(bk, k)) {
                bv = v;
                replaced = true;
            }
        }
        if (!replaced) {
            base.emplace_back(k, v);
        }
    }
}

} // namespace

std::string_view vdata_field(const VdataFields& f, std::string_view key) {
    for (const auto& [k, v] : f) {
        if (iequals(k, key)) {
            return v;
        }
    }
    return {};
}

std::vector<StatCategory> parse_stats(const KvDocument& doc) {
    std::vector<StatCategory> out;
    each_root(doc, "StatData", [&](const KvNode& root) {
        for (const KvNode& cat : root.children) {
            if (!cat.is_block || iequals(cat.key, "General")) {
                continue;
            }
            StatCategory c;
            c.key = cat.key;
            c.name = text(cat, "Name");
            c.internal_name = text(cat, "InternalName");
            if (const KvNode* costs = cat.find("Costs")) {
                c.new_cost = text(*costs, "New");
                c.raise_cost = text(*costs, "Raise");
            }
            each_block(cat, "Stat", [&](const KvNode& s) {
                StatDef d;
                d.name = text(s, "Name");
                d.internal_name = text(s, "InternalName");
                d.help_text = text(s, "HelpText");
                d.name_mapping = text(s, "NameMapping");
                d.min = number(s, "Min");
                d.max = number(s, "Max");
                d.default_value = number(s, "Default");
                d.disabled = flag(s, "Disabled");
                if (const KvNode* costs = s.find("Costs")) {
                    d.raise_cost = text(*costs, "Raise");
                }
                c.stats.push_back(std::move(d));
            });
            out.push_back(std::move(c));
        }
    });
    return out;
}

std::vector<FeatDef> parse_feats(const KvDocument& doc) {
    std::vector<FeatDef> out;
    each_root(doc, "FeatData", [&](const KvNode& root) {
        each_block(root, "Feats", [&](const KvNode& feats) {
            each_block(feats, "Feat", [&](const KvNode& f) {
                FeatDef d;
                d.name = text(f, "Name");
                d.internal_name = text(f, "InternalName");
                d.help_text = text(f, "HelpText");
                d.bases = numbered(f, "Base");
                d.pc_weighting = text(f, "PCWeighting");
                d.npc_weighting = text(f, "NPCWeighting");
                d.max_value = number(f, "MaxValue");
                d.disabled = flag(f, "Disabled");
                out.push_back(std::move(d));
            });
        });
    });
    return out;
}

std::vector<CharTemplate> parse_char_templates(const KvDocument& doc) {
    std::vector<CharTemplate> out;
    each_root(doc, "ClanDataTables", [&](const KvNode& root) {
        each_block(root, "ClanData", [&](const KvNode& c) {
            CharTemplate t;
            if (const KvNode* tx = c.find("Text")) {
                t.template_name = text(*tx, "TemplateName");
                t.parent = text(*tx, "ParentTemplateName");
                t.name = text(*tx, "Name");
                t.description = text(*tx, "Description");
            }
            t.general = fields(c.find("General"));
            t.attributes = fields(c.find("Attributes"));
            t.abilities = fields(c.find("Abilities"));
            t.disciplines = fields(c.find("Disciplines"));
            t.numina = fields(c.find("Numina"));
            t.resistances = fields(c.find("Resistances"));
            t.loiter_activities = fields(c.find("LoiterActivities"));
            if (const KvNode* r = c.find("Reactions")) {
                t.reactions_to = fields(r->find("To"));
            }
            out.push_back(std::move(t));
        });
    });
    return out;
}

CharTemplate resolve_char_template(const std::vector<CharTemplate>& templates, std::string_view name) {
    auto find = [&](std::string_view n) -> const CharTemplate* {
        for (const CharTemplate& t : templates) {
            if (iequals(t.template_name, n)) {
                return &t;
            }
        }
        return nullptr;
    };
    // Collect the chain from the template up to its root.
    std::vector<const CharTemplate*> chain;
    std::set<std::string> seen;
    for (const CharTemplate* t = find(name); t && seen.insert(t->template_name).second;
         t = t->parent.empty() ? nullptr : find(t->parent)) {
        chain.push_back(t);
    }
    if (chain.empty()) {
        return {};
    }
    CharTemplate out;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const CharTemplate& t = **it;
        if (!t.name.empty()) {
            out.name = t.name;
        }
        if (!t.description.empty()) {
            out.description = t.description;
        }
        merge(out.general, t.general);
        merge(out.attributes, t.attributes);
        merge(out.abilities, t.abilities);
        merge(out.disciplines, t.disciplines);
        merge(out.numina, t.numina);
        merge(out.resistances, t.resistances);
        merge(out.reactions_to, t.reactions_to);
        merge(out.loiter_activities, t.loiter_activities);
    }
    out.template_name = chain.front()->template_name;
    out.parent = chain.front()->parent;
    return out;
}

std::vector<Quest> parse_quests(const KvDocument& doc) {
    std::vector<Quest> out;
    each_root(doc, "QuestTable", [&](const KvNode& root) {
        each_block(root, "Quest", [&](const KvNode& q) {
            Quest quest;
            quest.title = text(q, "Title");
            quest.display_name = text(q, "DisplayName");
            each_block(q, "CompletionState", [&](const KvNode& s) {
                quest.states.push_back({number(s, "ID"), text(s, "Description"), text(s, "Type"),
                                        number(s, "AwardXP"), number(s, "AwardMoney"), text(s, "Event")});
            });
            out.push_back(std::move(quest));
        });
    });
    return out;
}

std::vector<Vendor> parse_vendors(const KvDocument& doc) {
    std::vector<Vendor> out;
    each_root(doc, "VendorData", [&](const KvNode& root) {
        each_block(root, "Vendor", [&](const KvNode& v) {
            Vendor vendor;
            vendor.name = text(v, "Name");
            vendor.internal_name = text(v, "InternalName");
            vendor.image = text(v, "VendorImage");
            each_block(v, "VendorItem", [&](const KvNode& i) {
                vendor.items.push_back({text(i, "Name"), text(i, "InternalName"), flag(i, "AmmoOnly"),
                                        number(i, "MinStage"), number(i, "MinVendorState")});
            });
            out.push_back(std::move(vendor));
        });
    });
    return out;
}

std::vector<DisciplinePower> parse_discipline_powers(const KvDocument& doc) {
    std::vector<DisciplinePower> out;
    auto read = [&](const KvNode& p) {
        DisciplinePower d;
        d.name = text(p, "Name");
        d.internal_name = text(p, "InternalName");
        d.discipline = text(p, "Discipline");
        d.level = number(p, "Level");
        d.blood_cost = text(p, "BloodCost");
        d.recovery_time = text(p, "RecoveryTime");
        d.overt = flag(p, "Overt");
        d.block = p;
        out.push_back(std::move(d));
    };
    each_root(doc, "DisciplineTgtList", [&](const KvNode& root) { each_block(root, "DisciplineTgt", read); });
    return out;
}

std::vector<LevelingTemplate> parse_leveling_templates(const KvDocument& doc) {
    std::vector<LevelingTemplate> out;
    each_root(doc, "LevelingTemplateList", [&](const KvNode& root) {
        each_block(root, "LevelingTemplate", [&](const KvNode& t) {
            LevelingTemplate lt;
            lt.name = text(t, "Name");
            lt.internal_name = text(t, "InternalName");
            lt.clan_dependency = text(t, "ClanDependency");
            each_block(t, "Level", [&](const KvNode& l) { lt.levels.push_back(fields(&l)); });
            each_block(t, "LevelGroup", [&](const KvNode& g) {
                LevelingTemplate::Group group;
                group.dependency = text(g, "Dependency");
                each_block(g, "Level", [&](const KvNode& l) { group.levels.push_back(fields(&l)); });
                lt.level_groups.push_back(std::move(group));
            });
            out.push_back(std::move(lt));
        });
    });
    return out;
}

std::vector<HistoryDef> parse_histories(const KvDocument& doc) {
    std::vector<HistoryDef> out;
    each_root(doc, "HistoryDataTables", [&](const KvNode& root) {
        each_block(root, "HistoryData", [&](const KvNode& h) {
            HistoryDef d;
            if (const KvNode* g = h.find("General")) {
                d.internal_name = text(*g, "InternalName");
                d.effect = text(*g, "Effect");
                d.critter_scope = text(*g, "CritterScope");
                d.genders_disallowed = text(*g, "Genders_Disallowed");
            }
            if (const KvNode* tx = h.find("Text")) {
                d.name = text(*tx, "Name");
                d.description = text(*tx, "Description");
                d.short_description = text(*tx, "ShortDescription");
                d.short_penalty_description = text(*tx, "ShortPenaltyDescription");
            }
            out.push_back(std::move(d));
        });
    });
    return out;
}

ItemTables parse_item_tables(const KvDocument& doc) {
    ItemTables out;
    each_root(doc, "ItemTypeData", [&](const KvNode& root) {
        each_block(root, "InventorySections", [&](const KvNode& list) {
            each_block(list, "InventorySection", [&](const KvNode& s) {
                const KvNode* displayed = s.find("IsDisplayed");
                out.inventory_sections.emplace_back(text(s, "InternalName"), !displayed || flag(s, "IsDisplayed"));
            });
        });
        each_block(root, "ItemTypes", [&](const KvNode& list) {
            each_block(list, "ItemType", [&](const KvNode& t) {
                out.item_types.push_back({text(t, "Name"), text(t, "InternalName"), text(t, "InventorySection"),
                                          flag(t, "IsWeapon"), flag(t, "IsWielded"), flag(t, "IsWorn")});
            });
        });
        each_block(root, "StartingEquipTables", [&](const KvNode& list) {
            each_block(list, "StartingEquip", [&](const KvNode& e) {
                StartingEquipment s{text(e, "Name"), text(e, "InternalName"), number(e, "Money"), {}};
                if (const KvNode* items = e.find("Items")) {
                    for (const KvNode& i : items->children) {
                        if (!i.is_block && iequals(i.key, "Item")) {
                            s.items.push_back(i.value);
                        }
                    }
                }
                out.starting_equipment.push_back(std::move(s));
            });
        });
        each_block(root, "BotchTypes", [&](const KvNode& list) {
            each_block(list, "BotchType", [&](const KvNode& b) { out.botch_types.push_back(fields(&b)); });
        });
        each_block(root, "ActivationTypes", [&](const KvNode& list) {
            each_block(list, "ActivationType", [&](const KvNode& a) { out.activation_types.push_back(fields(&a)); });
        });
    });
    return out;
}

std::string_view vdata_lookup(const KvDocument& doc, std::string_view path) {
    const KvNode* node = nullptr;
    const std::vector<KvNode>* level = &doc.roots;
    while (!path.empty()) {
        const std::size_t slash = path.find('/');
        const std::string_view part = path.substr(0, slash);
        node = nullptr;
        for (const KvNode& n : *level) {
            if (iequals(n.key, part)) {
                node = &n;
                break;
            }
        }
        if (!node) {
            return {};
        }
        level = &node->children;
        path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
    }
    return node && !node->is_block ? std::string_view(node->value) : std::string_view();
}

float vdata_number(const KvDocument& doc, std::string_view path, float fallback) {
    const std::string s(vdata_lookup(doc, path));
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    return s.empty() || end == s.c_str() ? fallback : v;
}

std::vector<float> vdata_series(const KvNode& block, std::string_view prefix) {
    std::vector<float> out;
    for (int i = 0;; ++i) {
        const std::string v = text(block, std::string(prefix) + std::to_string(i));
        if (v.empty()) {
            return out;
        }
        out.push_back(std::strtof(v.c_str(), nullptr));
    }
}

std::vector<RuleTable> parse_rule_tables(const KvDocument& doc) {
    std::vector<RuleTable> out;
    each_root(doc, "RuleData", [&](const KvNode& root) {
        each_block(root, "Tables", [&](const KvNode& tables) {
            each_block(tables, "Table", [&](const KvNode& t) {
                out.push_back({text(t, "Name"), text(t, "InternalName"), flag(t, "Clamping"), vdata_series(t)});
            });
        });
    });
    return out;
}

std::vector<TraitEffectGroup> parse_trait_effects(const KvDocument& doc) {
    std::vector<TraitEffectGroup> out;
    each_root(doc, "TraitEffectsData", [&](const KvNode& root) {
        each_block(root, "TraitEffectCategory", [&](const KvNode& cat) {
            each_block(cat, "TraitEffectGroup", [&](const KvNode& g) {
                TraitEffectGroup group{text(cat, "InternalName"), text(g, "InternalName"), {}};
                each_block(g, "TraitEffect", [&](const KvNode& e) {
                    TraitEffect effect{text(e, "Trait"), text(e, "Modifier"), text(e, "DisplayOverride"), {}, {}};
                    if (const KvNode* costs = e.find("Costs")) {
                        effect.new_cost = text(*costs, "New");
                        effect.raise_cost = text(*costs, "Raise");
                    }
                    group.effects.push_back(std::move(effect));
                });
                out.push_back(std::move(group));
            });
        });
    });
    return out;
}

std::vector<ReactionGroup> parse_reactions(const KvDocument& doc) {
    std::vector<ReactionGroup> out;
    each_root(doc, "ReactionsData", [&](const KvNode& root) {
        each_block(root, "ReactionCategory", [&](const KvNode& cat) {
            each_block(cat, "ReactionGroup", [&](const KvNode& g) {
                ReactionGroup group{text(cat, "InternalName"), text(g, "InternalName"), {}};
                each_block(g, "Reaction", [&](const KvNode& r) {
                    group.reactions.push_back({text(r, "Targets"), text(r, "WhoModifies"), text(r, "Modifier")});
                });
                out.push_back(std::move(group));
            });
        });
    });
    return out;
}

StealthTables parse_stealth_tables(const KvDocument& doc) {
    StealthTables out;
    each_root(doc, "StealthData", [&](const KvNode& root) {
        auto grid = [&](std::string_view key, std::vector<std::vector<float>>& rows) {
            if (const KvNode* table = root.find(key)) {
                for (int light = 0;; ++light) {
                    const KvNode* row = table->find("Light" + std::to_string(light));
                    if (!row) {
                        break;
                    }
                    rows.push_back(vdata_series(*row, "Stealth"));
                }
            }
        };
        grid("StealthVisionScalarTable", out.vision_scalar);
        grid("StealthVisionConeScalarTable", out.vision_cone_scalar);
        if (const KvNode* t = root.find("StealthHearingDistTable")) {
            out.hearing_distance = vdata_series(*t, "Stealth");
        }
        if (const KvNode* t = root.find("StealthLightRangeTable")) {
            out.light_range = vdata_series(*t, "Light");
        }
    });
    return out;
}

DiceRollTables parse_dice_rolls(const KvDocument& doc) {
    DiceRollTables out;
    each_root(doc, "DiceRollData", [&](const KvNode& root) {
        if (const KvNode* rules = root.find("Rules")) {
            if (const KvNode* h = rules->find("HealthModifiers")) {
                out.health_modifiers = vdata_series(*h);
            }
            if (const KvNode* w = rules->find("TableWeightings")) {
                for (const KvNode& t : w->children) {
                    if (t.is_block) {
                        out.weightings.emplace_back(t.key, vdata_series(t));
                    }
                }
            }
        }
        if (const KvNode* tx = root.find("Text")) {
            auto names = [&](std::string_view key, std::vector<std::string>& v) {
                if (const KvNode* b = tx->find(key)) {
                    v = numbered(*b, "Name");
                }
            };
            names("Difficulty", out.difficulty_names);
            names("Success", out.success_names);
            names("RollResult", out.roll_result_names);
        }
    });
    return out;
}

std::vector<Disposition> parse_dispositions(const KvDocument& doc) {
    std::vector<Disposition> out;
    each_root(doc, "DispositionTable", [&](const KvNode& root) {
        for (const KvNode& d : root.children) {
            if (!d.is_block) {
                continue;
            }
            out.push_back({d.key, fields(&d), fields(d.find("DefaultExpression")), fields(d.find("EyeTarget")),
                           fields(d.find("LoudExpressions"))});
        }
    });
    return out;
}

} // namespace openbl::formats
