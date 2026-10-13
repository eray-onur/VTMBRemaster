#pragma once

// Where the simulation hands Python statements: the ones entity outputs carry
// (Bloodlines' seventh output field). The Python 3 host with its translator
// for the game's Python 2.1 scripts implements this (docs/ARCHITECTURE.md);
// RecordingScriptHost keeps the statements for tests and tools.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openbl::sim {

using EntityId = std::uint32_t;
constexpr EntityId kNoEntity = UINT32_MAX;

// What a statement runs for: the entity whose output fired it, and that
// output's activator.
struct ScriptContext {
    EntityId caller = kNoEntity;
    EntityId activator = kNoEntity;
};

class ScriptHost {
public:
    virtual ~ScriptHost() = default;
    virtual void run(std::string_view statement, const ScriptContext& context) = 0;
};

class RecordingScriptHost final : public ScriptHost {
public:
    struct Call {
        std::string statement;
        ScriptContext context;
    };
    void run(std::string_view statement, const ScriptContext& context) override {
        calls.push_back({std::string(statement), context});
    }
    std::vector<Call> calls;
};

} // namespace openbl::sim
