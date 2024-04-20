#include "include\PerkEntryPointExtenderAPI.h"

void SetupLog() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");

    auto pluginName = Version::PROJECT;
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));

    spdlog::set_default_logger(std::move(loggerPtr));
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);

    //Pattern
    spdlog::set_pattern("%v");
}

class EntryVisitor : public RE::PerkEntryVisitor {
public:
    RE::Actor* summoner;
    RE::BSTArray<RE::CommandedActorData> commandedActorData;

    RE::BSContainer::ForEachResult Visit(RE::BGSPerkEntry* a_entry) override {
        return RE::BSContainer::ForEachResult::kContinue;
    }

    //Note - these are ALWAYS filled. It's checked earlier on.
    EntryVisitor(RE::Actor* a_summoner, RE::BSTArray<RE::CommandedActorData> a_commandedActorData) {
        this->summoner = a_summoner;
        this->commandedActorData = a_commandedActorData;
    }
};

struct Hooks {
    struct CommandedActorLimitHook {
        static void thunk(RE::PerkEntryPoint entry_point, RE::Actor* target, RE::MagicItem* a_spell, void* out) {
  
            float* floatPtr = static_cast<float*>(out);
            *floatPtr = 999.0f;  // If you need more than 999 summons, I think you've got a problem
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct CommandedActorHook {
        static void thunk(RE::AIProcess* a_AIProcess, RE::ActiveEffectReferenceEffectController* a_activeEffectController, void* a_out) {
            func(a_AIProcess, a_activeEffectController, a_out);

            //Validate that the effect has a valid base spell and magic effect.
            auto* baseActiveEffect = a_activeEffectController->effect ? a_activeEffectController->effect : nullptr;
            auto* baseSpell = baseActiveEffect ? baseActiveEffect->spell : nullptr;
            auto* baseEffect = baseSpell ? baseActiveEffect->effect : nullptr;
            auto* baseMagicEffect = baseEffect ? baseEffect->baseEffect : nullptr;
            if (!baseMagicEffect) return;

            //Validate that there is a summoner.
            auto* midHigh = a_AIProcess->middleHigh ? a_AIProcess->middleHigh : nullptr;
            auto commandedActors = midHigh ? midHigh->commandedActors : RE::BSTArray<RE::CommandedActorData>();
            auto* summoner = commandedActors.empty() ? nullptr : a_AIProcess->GetUserData();
            if (!summoner) return;

            //Float representing the limit for the number of summons of specific type.
            float limit = 1.0f;
            RE::BGSEntryPoint::HandleEntryPoint(RE::PerkEntryPoint::kModCommandedActorLimit, summoner, baseSpell, &limit);

            //The actual logic is processes inside the perkEntryVisitor object. Which is deleted at the end.
            auto entryVisitor = EntryVisitor(summoner, commandedActors);
            RE::PerkEntryVisitor& perkEntryVisitor = entryVisitor;

            summoner->ForEachPerkEntry(RE::BGSEntryPoint::ENTRY_POINT::kModCommandedActorLimit, perkEntryVisitor);
            delete &perkEntryVisitor;
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };
    static void Install() {
        //Hooks where the game looks for the actor's summon limit. Changes it to a static value.
        REL::Relocation<std::uintptr_t> functionCommandedActorLimitHook{RELOCATION_ID(38993, 40056),
                                                                        REL::Relocate(0xA1, 0xEC)};
        stl::write_thunk_call<CommandedActorLimitHook>(functionCommandedActorLimitHook.address());

        //Hooks where the game processes the attempt to command an actor to replace the logic.
        REL::Relocation<std::uintptr_t> functionCommandedActorHook{RELOCATION_ID(38904, 39950),
                                                                   REL::Relocate(0x14B, 0x12B)};
        stl::write_thunk_call<CommandedActorHook>(functionCommandedActorHook.address());
    };
};

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    Hooks::Install();
    return true;
}