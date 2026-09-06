#include "PCH.h"

#include "ActorStateAccess.h"
#include "Hooks.h"
#include "Policy.h"

namespace ConditionalArrowEmbedding::Hooks {
namespace {
Config g_config{};
inline constexpr RE::FormID ActorTypeCreatureFormID = 0x00013795;
inline constexpr RE::FormID ActorTypeGhostFormID = 0x000D205E;
inline constexpr RE::FormID InvisibleRaceFormID = 0x00071E6A;
inline constexpr RE::FormID ManakinRaceFormID = 0x0010760A;
inline constexpr std::uint64_t PhysicalHitDispatcherAddressId = 38627;
inline constexpr std::uintptr_t PhysicalHitCallOffset = 0x4A8;

enum class TelemetryEvent : std::uint32_t {
	NormalArrowReached = 1U << 0U,
	PolicyDecision = 1U << 1U,
	BounceApplied = 1U << 2U,
	MissingImpact = 1U << 3U,
	HandlerFailure = 1U << 4U,
	PlayerEligibility = 1U << 5U,
};

std::atomic_uint32_t g_loggedTelemetry{};

[[nodiscard]] bool ClaimTelemetry(const TelemetryEvent a_event) noexcept {
	const auto bit = static_cast<std::uint32_t>(a_event);
	return (g_loggedTelemetry.fetch_or(bit, std::memory_order_relaxed) & bit) == 0U;
}

[[nodiscard]] bool IsExpectedPhysicalHitCallsite(const std::uintptr_t a_address) noexcept {
	// SkyrimSE 1.7.104, the shared physical-hit dispatcher at Address Library
	// ID 38627. The call receives (Actor*, HitData&) and applies the completed
	// HitData. Refuse to patch if the reviewed instruction boundary changed.
	// The rel32 target is deliberately allowed to vary so another SKSE hook can
	// remain in the call chain.
	constexpr std::array<std::uint8_t, 8> prefix{0x48, 0x8D, 0x54, 0x24, 0x50, 0x48, 0x8B, 0xCF};
	constexpr std::array<std::uint8_t, 9> suffix{0xF3, 0x0F, 0x10, 0x8C, 0x24, 0xCC, 0x00, 0x00, 0x00};
	return std::equal(prefix.begin(), prefix.end(),
	                  reinterpret_cast<const std::uint8_t *>(a_address - prefix.size())) &&
	       *reinterpret_cast<const std::uint8_t *>(a_address) == 0xE8 &&
	       std::equal(suffix.begin(), suffix.end(), reinterpret_cast<const std::uint8_t *>(a_address + 5));
}

[[nodiscard]] bool IsEmbedResult(const RE::ImpactResult a_result) noexcept {
	return a_result == RE::ImpactResult::kStick || a_result == RE::ImpactResult::kImpale;
}

[[nodiscard]] bool HasSkyrimKeyword(const RE::Actor &a_target, const RE::FormID a_formID) {
	const auto *keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(a_formID);
	return keyword && a_target.HasKeyword(keyword);
}

[[nodiscard]] TargetTypeTraits CollectTargetTypeTraits(RE::Actor &a_target) {
	const auto *race = a_target.GetRace();
	const auto raceFormID = race ? race->GetFormID() : 0;
	return TargetTypeTraits{
	    .hasRace = race != nullptr,
	    .actorTypeNPC = a_target.IsHumanoid(),
	    .actorTypeAnimal = a_target.HasKeywordWithType(RE::DefaultObjectID::kKeywordAnimal),
	    .actorTypeCreature = HasSkyrimKeyword(a_target, ActorTypeCreatureFormID),
	    .actorTypeDaedra = a_target.HasKeywordWithType(RE::DefaultObjectID::kKeywordDaedra),
	    .actorTypeDragon = a_target.IsDragon(),
	    .actorTypeDwarven = a_target.HasKeywordWithType(RE::DefaultObjectID::kKeywordRobot),
	    .actorTypeGhost = HasSkyrimKeyword(a_target, ActorTypeGhostFormID),
	    .actorTypeUndead = a_target.HasKeywordWithType(RE::DefaultObjectID::kKeywordUndead),
	    .ghost = a_target.IsGhost(),
	    .reanimated = IsRuntimeReanimated(a_target),
	    .excludedVanillaUtilityRace = raceFormID == InvisibleRaceFormID || raceFormID == ManakinRaceFormID,
	};
}

[[nodiscard]] RE::Projectile::ImpactData *FindCurrentImpact(RE::Projectile &a_projectile,
                                                            const RE::Actor &a_target) noexcept {
	// AddImpact installs the collision currently being handled at the list head.
	// HandleHits marks that record processed (unk48 = 1) only after actor damage
	// returns. Never scan older entries: a prior collision with the same actor can
	// otherwise be mistaken for this hit and corrupt bounce processing.
	auto &projectileData = a_projectile.GetProjectileRuntimeData();
	if (projectileData.flags.any(RE::Projectile::Flags::kProcessedImpacts) ||
	    projectileData.flags.any(RE::Projectile::Flags::kDestroyed)) {
		return nullptr;
	}
	auto &impacts = projectileData.impacts;
	if (impacts.empty()) {
		return nullptr;
	}
	auto *impact = impacts.front();
	if (!impact || impact->unk48 != 0) {
		return nullptr;
	}
	auto collidee = impact->collidee.get();
	if (!collidee || collidee.get() != std::addressof(a_target)) {
		return nullptr;
	}
	return impact;
}

[[nodiscard]] std::vector<std::string> CollectNodeNames(const RE::Projectile::ImpactData *a_impact,
                                                        const std::uint32_t a_maxAncestorDepth) {
	std::vector<std::string> names;
	if (!a_impact || !a_impact->damageRootNode) {
		return names;
	}
	names.reserve(static_cast<std::size_t>(a_maxAncestorDepth) + 1);
	const RE::NiAVObject *current = a_impact->damageRootNode;
	for (std::uint32_t depth = 0; current && depth <= a_maxAncestorDepth; ++depth) {
		if (!current->name.empty()) {
			names.emplace_back(current->name.c_str());
		}
		current = current->parent;
	}
	return names;
}

void ApplyPostDamageDecision(RE::Actor &a_target, RE::HitData &a_hitData, const bool a_targetWasAlive,
                             const bool a_targetIsLivingHumanoid, RE::Projectile &a_projectile) {
	if (a_projectile.GetFormType() != RE::FormType::ProjectileArrow) {
		return;
	}
	auto *missile = a_projectile.As<RE::MissileProjectile>();
	if (!missile) {
		return;
	}

	auto *impact = FindCurrentImpact(a_projectile, a_target);
	if (!impact) {
		if (ClaimTelemetry(TelemetryEvent::MissingImpact)) {
			logger::warn("runtime telemetry: normal arrow reached the physical-hit hook, but no matching "
			             "projectile impact was available; vanilla preserved");
		}
		if (g_config.debugLogging) {
			logger::debug("arrow {:08X}: no matching actor impact; preserving vanilla",
			              a_projectile.GetFormID());
		}
		return;
	}

	auto &missileData = missile->GetMissileRuntimeData();
	const auto missileResultBefore = missileData.impactResult;
	const auto impactResultBefore = impact->impactResult;
	// ProcessImpacts dispatches on the missile-wide result. The per-impact value
	// is synchronized only when applying our bounce; it must not turn a
	// missile-wide Destroy/Bounce result into a different action.
	const bool vanillaWouldEmbed = IsEmbedResult(missileResultBefore);
	const bool blockedOrRicocheting =
	    a_hitData.flags.any(RE::HitData::Flag::kBlocked) || a_hitData.flags.any(RE::HitData::Flag::kRicochet);
	const auto healthAfter =
	    static_cast<double>(a_target.AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth));
	const bool engineReportsDead = a_target.IsDead();
	const bool killedByHit = WasKilledByHit(PostDamageState{
	    .targetWasAlive = a_targetWasAlive,
	    .targetReportsDead = engineReportsDead,
	});
	if (g_config.debugLogging && a_hitData.flags.any(RE::HitData::Flag::kFatal) != engineReportsDead) {
		logger::debug("arrow {:08X} target {:08X}: precomputed fatal flag disagrees with post-hit "
		              "engine death state; using engine state",
		              a_projectile.GetFormID(), a_target.GetFormID());
	}
	const auto names = CollectNodeNames(impact, g_config.fallbackHeadAncestorDepth);
	const auto region = ClassifyHitRegion(static_cast<std::int32_t>(a_hitData.damageLimb.underlying()), names,
	                                      g_config.fallbackHeadNodeTokens);

	const auto maximumHealth = static_cast<double>(a_target.GetActorValueMax(RE::ActorValue::kHealth));
	const auto healthRatioAfter = maximumHealth > 0.0 && std::isfinite(maximumHealth)
	                                  ? std::clamp(healthAfter / maximumHealth, 0.0, 1.0)
	                                  : std::numeric_limits<double>::quiet_NaN();

	const ImpactContext context{.enabled = g_config.enabled,
	                            .targetWasAlive = a_targetWasAlive,
	                            .targetKilledByHit = killedByHit,
	                            .targetIsLivingHumanoid = a_targetIsLivingHumanoid,
	                            .vanillaWouldEmbed = vanillaWouldEmbed,
	                            .blockedOrAlreadyRicocheting = blockedOrRicocheting,
	                            .targetIsPlayer = a_target.IsPlayerRef(),
	                            .affectPlayer = g_config.affectPlayer,
	                            .affectNPCs = g_config.affectNPCs,
	                            .region = region,
	                            .healthRatioAfter = healthRatioAfter,
	                            .bodyStickBelowHealthRatio = g_config.bodyStickBelowHealthRatio};
	const auto action = DecideImpact(context);
	if (ClaimTelemetry(TelemetryEvent::PolicyDecision)) {
		logger::info("runtime telemetry: first matched arrow decision target={:08X} livingHumanoid={} "
		             "region={} postHitHealthRatio={:.4f} killedByHit={} missileResult={} impactResult={} "
		             "action={}",
		             a_target.GetFormID(), a_targetIsLivingHumanoid, ToString(region), healthRatioAfter,
		             killedByHit, static_cast<std::int32_t>(missileResultBefore),
		             static_cast<std::int32_t>(impactResultBefore), ToString(action));
	}
	if (action == ImpactAction::Bounce) {
		missileData.impactResult = RE::ImpactResult::kBounce;
		impact->impactResult = RE::ImpactResult::kBounce;
		if (ClaimTelemetry(TelemetryEvent::BounceApplied)) {
			logger::info("runtime telemetry: first conditional bounce applied to arrow {:08X} and target "
			             "{:08X}",
			             a_projectile.GetFormID(), a_target.GetFormID());
		}
	}

	if (g_config.debugLogging) {
		logger::debug("arrow {:08X} target {:08X}: livingHumanoid={} region={} healthAfter={:.4f} "
		              "killedByHit={} vanillaEmbed={} action={}",
		              a_projectile.GetFormID(), a_target.GetFormID(), a_targetIsLivingHumanoid,
		              ToString(region), healthRatioAfter, killedByHit, vanillaWouldEmbed, ToString(action));
	}
}

struct ProjectileActorHitHook {
	static void Thunk(RE::Actor *a_target, RE::HitData &a_hitData) {
		const bool targetWasAlive =
		    a_target && !a_target->IsDead() &&
		    a_target->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) > 0.0F;
		// Keep the smart pointer alive across the original hit processor; damage
		// callbacks are allowed to release references.
		auto source = a_hitData.sourceRef.get();
		auto *projectile = source ? source->As<RE::ArrowProjectile>() : nullptr;
		const auto traits = a_target && projectile ? CollectTargetTypeTraits(*a_target) : TargetTypeTraits{};
		const bool targetIsLivingHumanoid = IsLivingHumanoidTarget(traits);
		if (a_target && projectile && a_target->IsPlayerRef() &&
		    ClaimTelemetry(TelemetryEvent::PlayerEligibility)) {
			const auto *race = a_target->GetRace();
			logger::info("runtime telemetry: player eligibility race={:08X} livingHumanoid={} "
			             "hasRace={} npc={} animal={} creature={} daedra={} dragon={} dwarven={} "
			             "ghostKeyword={} undead={} ghostState={} reanimated={} utilityRace={}",
			             race ? race->GetFormID() : 0, targetIsLivingHumanoid, traits.hasRace,
			             traits.actorTypeNPC, traits.actorTypeAnimal, traits.actorTypeCreature,
			             traits.actorTypeDaedra, traits.actorTypeDragon, traits.actorTypeDwarven,
			             traits.actorTypeGhost, traits.actorTypeUndead, traits.ghost, traits.reanimated,
			             traits.excludedVanillaUtilityRace);
		}

		Func(a_target, a_hitData);
		try {
			if (a_target && projectile) {
				if (ClaimTelemetry(TelemetryEvent::NormalArrowReached)) {
					logger::info("runtime telemetry: first normal arrow reached the post-damage physical-hit "
					             "hook");
				}
				ApplyPostDamageDecision(*a_target, a_hitData, targetWasAlive, targetIsLivingHumanoid,
				                        *projectile);
			}
		} catch (const std::exception &error) {
			if (ClaimTelemetry(TelemetryEvent::HandlerFailure)) {
				logger::error("post-damage arrow decision failed safely; further identical errors are "
				              "suppressed: {}",
				              error.what());
			}
		} catch (...) {
			if (ClaimTelemetry(TelemetryEvent::HandlerFailure)) {
				logger::error("post-damage arrow decision failed safely with an unknown exception; further "
				              "identical errors are suppressed");
			}
		}
	}

	static inline REL::Relocation<decltype(Thunk)> Func;
};
} // namespace

bool Install(Config a_config) {
	g_config = std::move(a_config);
	if (!g_config.enabled) {
		logger::info("feature disabled by configuration; no runtime hook installed");
		return true;
	}

	// Verified against SkyrimSE.exe 1.7.104.0. The normal
	// ArrowProjectile::HandleHits actor branch reaches this shared dispatcher
	// after AddImpact has selected an impact result and before ProcessImpacts
	// consumes it.
	const REL::Relocation<std::uintptr_t> physicalHitDispatcher{REL::ID(PhysicalHitDispatcherAddressId)};
	const auto callsite = physicalHitDispatcher.address() + PhysicalHitCallOffset;
	if (!IsExpectedPhysicalHitCallsite(callsite)) {
		logger::critical("physical-hit callsite signature mismatch at 0x{:X}; hook not installed", callsite);
		return false;
	}

	SKSE::AllocTrampoline(64);
	auto &trampoline = SKSE::GetTrampoline();
	ProjectileActorHitHook::Func = trampoline.write_call<5>(callsite, ProjectileActorHitHook::Thunk);
	logger::info("post-damage physical-hit hook installed at ID {} + 0x{:X}; body threshold={:.3f}; "
	             "default runtime telemetry is bounded to six first-occurrence messages",
	             PhysicalHitDispatcherAddressId, PhysicalHitCallOffset, g_config.bodyStickBelowHealthRatio);
	return true;
}
} // namespace ConditionalArrowEmbedding::Hooks
