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
        static void thunk(RE::BGSEntryPoint entry_point, RE::Actor* target, RE::MagicItem* a_spell, void* out) {
  
            float* floatPtr = static_cast<float*>(out);
            *floatPtr = 25.0f;  // If you need more than 25 summons, I know you've got a problem
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct CommandedActorHook {
        static void thunk(RE::AIProcess* a_AIProcess, RE::ActiveEffectReferenceEffectController* a_activeEffectController, void* a_out) {
            func(a_AIProcess, a_activeEffectController, a_out);
            if (!(a_AIProcess && a_activeEffectController)) return;

            //Validate that the effect has a valid base spell and magic effect.
            auto* baseActiveEffect = a_activeEffectController->effect ? a_activeEffectController->effect : nullptr;
            auto* baseSpell = baseActiveEffect ? baseActiveEffect->spell : nullptr;
            auto* baseEffect = baseSpell ? baseActiveEffect->effect : nullptr;
            auto* baseMagicEffect = baseEffect ? baseEffect->baseEffect : nullptr;
            if (!baseMagicEffect) return;

            //Validate that there is a summoner.
            auto* midHigh = a_AIProcess->middleHigh ? a_AIProcess->middleHigh : nullptr;
            auto* summoner = midHigh ? a_AIProcess->GetUserData() : nullptr;
            if (!summoner) return;

            //The actual logic is processes inside the perkEntryVisitor object. Which is deleted at the end.
            _loggerInfo("Creating derrived visitor");
            auto commandedActors = midHigh->commandedActors;
            auto entryVisitor = EntryVisitor(summoner, commandedActors);
            _loggerInfo("Casting the visitor");
            RE::PerkEntryVisitor& perkEntryVisitor = entryVisitor;

            _loggerInfo("Preparing to go into the visitor");
            summoner->ForEachPerkEntry(RE::BGSEntryPoint::ENTRY_POINT::kModCommandedActorLimit, perkEntryVisitor);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };
    static void Install() {
        //Hooks where the game looks for the actor's summon limit. Changes it to a static value.
        REL::Relocation<std::uintptr_t> functionCommandedActorLimitHook{RELOCATION_ID(38993, 40056),
                                                                        OFFSET(0xA1, 0xEC)};
        stl::write_thunk_call<CommandedActorLimitHook>(functionCommandedActorLimitHook.address());

        //Hooks where the game processes the attempt to command an actor to replace the logic.
        REL::Relocation<std::uintptr_t> functionCommandedActorHook{RELOCATION_ID(38904, 39950),
                                                                   OFFSET(0x14B, 0x12B)};
        stl::write_thunk_call<CommandedActorHook>(functionCommandedActorHook.address());
    };
};

#ifdef SKYRIM_AE
extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({ Version::MAJOR, Version::MINOR, Version::PATCH });
    v.PluginName(Version::PROJECT);
    v.AuthorName("MaybeAsrak");
    v.UsesAddressLibrary();
    v.UsesNoStructs();
    v.CompatibleVersions({
        SKSE::RUNTIME_1_6_1130,
        _1_6_1170 });
    return v;
    }();
#else 
extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface * a_skse, SKSE::PluginInfo * a_info)
{
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = "SummonLimitFix";
    a_info->version = 1;

    const auto ver = a_skse->RuntimeVersion();
    if (ver
#	ifndef SKYRIMVR
        < SKSE::RUNTIME_1_5_39
#	else
        > SKSE::RUNTIME_VR_1_4_15_1
#	endif
        ) {
        SKSE::log::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
        return false;
    }

    return true;
}
#endif

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface * a_skse) {
    SetupLog();
    _loggerInfo("Starting up Summon Limit Fix.");
#ifdef SKYRIM_AE
    _loggerInfo("Plugin Version: {}.{}.{}", Version::MAJOR, Version::MINOR, Version::PATCH);
#else 
    _loggerInfo("Plugin Version: {}.{}.{}, 1.5 build.", Version::MAJOR, Version::MINOR, Version::PATCH);
    _loggerInfo("Do not report ANY issues with this version.");
#endif
    _loggerInfo("==================================================");

    SKSE::Init(a_skse);
    Hooks::Install();

    return true;
}