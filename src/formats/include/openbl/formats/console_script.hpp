#pragma once

// Console scripts (cfg/*.cfg, cfg/valve.rc) and the server-options layout
// script (scripts/settings.scr). See docs/formats/text_data.md. Neither
// parser throws; problems become diagnostics.

#include "openbl/formats/text_diagnostic.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openbl::formats {

// One console command: its name and arguments, with quotes removed.
struct ConsoleCommand {
    std::vector<std::string> tokens; // tokens[0] is the command or cvar name.
    std::size_t line = 0;            // 1-based.
};

// Commands are separated by line breaks and by ';' outside quotes. "//"
// starts a comment outside quotes. A quote left open ends at the line break.
struct ConsoleScript {
    std::vector<ConsoleCommand> commands;
    std::vector<TextDiagnostic> diagnostics;
};
ConsoleScript parse_console_script(std::string_view text);

// scripts/settings.scr: "VERSION n", then "DESCRIPTION name { options }", each
// option being "cvar" { "prompt" { TYPE info... } { "default" } }.
struct SettingsOption {
    std::string cvar, prompt;
    std::string type;                // BOOL, NUMBER, STRING or LIST.
    std::vector<std::string> info;   // NUMBER: min, max (-1 for none).
    std::vector<std::pair<std::string, std::string>> list; // LIST: label, value.
    std::string default_value;
};
struct SettingsScript {
    std::string version, description;
    std::vector<SettingsOption> options;
    std::vector<TextDiagnostic> diagnostics;
};
SettingsScript parse_settings_script(std::string_view text);

} // namespace openbl::formats
