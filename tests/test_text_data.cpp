#include "openbl/formats/keyvalues.hpp"
#include "openbl/vfs/game_filesystem.hpp"
#include "test_support.hpp"

#include <doctest/doctest.h>

#include <map>

using namespace openbl;
using namespace openbl::formats;

TEST_CASE("KeyValues integration: text data files in the real install") {
    auto game = test::game_dir();
    if (!game) {
        MESSAGE("OPENBL_GAME_DIR not set; skipping");
        return;
    }
    const vfs::GameFileSystem gfs(*game);
    struct Group {
        std::size_t files = 0, clean = 0, diagnostics = 0;
        std::map<std::string, std::string> roots; // root key -> example file
        std::vector<std::string> failing;
    };
    std::map<std::string, Group> groups;
    for (const auto& path : gfs.list_files()) {
        const bool txt = path.ends_with(".txt");
        std::string group;
        if (txt && path.starts_with("vdata/")) {
            group = path.substr(0, path.find('/', 6));
        } else if (txt && (path.starts_with("particles/") || path.starts_with("sound/schemes/") ||
                           path.starts_with("scripts/") || path.starts_with("resource/"))) {
            group = path.substr(0, path.find('/'));
        } else if (path.ends_with(".res")) {
            group = "*.res";
        } else {
            continue;
        }
        const auto data = gfs.read(path);
        const KvOptions options{.escaped_quotes = path.starts_with("vdata/")};
        const auto doc = parse_keyvalues({reinterpret_cast<const char*>(data->data()), data->size()}, options);
        auto& g = groups[group];
        ++g.files;
        g.clean += doc.diagnostics.empty();
        g.diagnostics += doc.diagnostics.size();
        if (!doc.diagnostics.empty() && g.failing.size() < 3) {
            g.failing.push_back(path + ": line " + std::to_string(doc.diagnostics[0].line) + " " +
                                doc.diagnostics[0].message);
        }
        for (const auto& r : doc.roots) {
            g.roots.emplace(r.key, path);
        }
    }
    for (const auto& [name, g] : groups) {
        std::string roots;
        for (const auto& [k, example] : g.roots) {
            if (roots.size() < 200) {
                roots += k + " ";
            }
        }
        MESSAGE(name << ": " << g.clean << "/" << g.files << " clean, " << g.diagnostics << " diagnostics; roots: "
                     << roots);
        for (const auto& f : g.failing) {
            MESSAGE("  " << f);
        }
    }
}
