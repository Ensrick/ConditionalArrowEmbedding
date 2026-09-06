#include "PCH.h"

#include "ActorStateAccess.h"
#include "DamageDispatch.h"
#include "DeferredImpactGate.h"
#include "Hooks.h"
#include "Policy.h"
#include "ProjectileLifecycle.h"

#include <bit>
#include <chrono>
#include <cstring>
#include <mutex>

namespace ConditionalArrowEmbedding::Hooks {
namespace {
Config g_config{};
inline constexpr RE::FormID ActorTypeCreatureFormID = 0x00013795;
inline constexpr RE::FormID ActorTypeGhostFormID = 0x000D205E;
inline constexpr RE::FormID InvisibleRaceFormID = 0x00071E6A;
inline constexpr RE::FormID ManakinRaceFormID = 0x0010760A;
inline constexpr std::uint64_t PhysicalHitDispatcherAddressId = 38627;
inline constexpr std::uintptr_t PhysicalHitCallOffset = 0x4A8;
inline constexpr std::uint64_t ActorProcessHitAddressId = 38586;
inline constexpr std::uintptr_t QueueSubmissionCallOffset = 0xA7;
inline constexpr std::uint64_t QueuedCommandRunnerAddressId = 36991;
inline constexpr std::uintptr_t QueuedActorHitCallOffset = 0xA3D;
inline constexpr std::uint64_t ArrowVtableAddressId = 209891;
inline constexpr std::size_t ProcessImpactsVtableIndex = 0xAC;
DeferredImpactGate g_deferredImpacts;
std::mutex g_deferredMutex;

[[nodiscard]] std::uint64_t NowMs() noexcept {
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
	                                      std::chrono::steady_clock::now().time_since_epoch())
	                                      .count());
}

enum class HitPhase { Immediate, QueuedCompletion };

enum class TelemetryEvent : std::uint32_t {
	NormalArrowReached = 1U << 0U,
	PolicyDecision = 1U << 1U,
	BounceApplied = 1U << 2U,
	MissingImpact = 1U << 3U,
	HandlerFailure = 1U << 4U,
	PlayerEligibility = 1U << 5U,
	DamageDeferred = 1U << 6U,
	QueuedDamageCompleted = 1U << 7U,
	LethalHitPreserved = 1U << 8U,
	VisualDeferred = 1U << 9U,
	VisualBounceApplied = 1U << 10U,
	DeferredRejected = 1U << 11U,
	QueuedImpactRegistered = 1U << 12U,
	QueuedRegistrationRejected = 1U << 13U,
};

std::atomic_uint32_t g_loggedTelemetry{};

[[nodiscard]] bool ClaimTelemetry(const TelemetryEvent a_event) noexcept {
	const auto bit = static_cast<std::uint32_t>(a_event);
	return (g_loggedTelemetry.fetch_or(bit, std::memory_order_relaxed) & bit) == 0U;
}

[[nodiscard]] bool IsExpectedPhysicalHitCallsite(const std::uintptr_t a_address) noexcept {
	// SkyrimSE 1.7.104, the shared physical-hit dispatcher at Address Library
	// ID 38627. The call receives (Actor*, HitData&) and submits the completed
	// HitData. ProcessHit can apply it OR queue it. Refuse an unreviewed boundary.
	// The original callee is also checked before installing all four hooks.
	constexpr std::array<std::uint8_t, 8> prefix{0x48, 0x8D, 0x54, 0x24, 0x50, 0x48, 0x8B, 0xCF};
	constexpr std::array<std::uint8_t, 9> suffix{0xF3, 0x0F, 0x10, 0x8C, 0x24, 0xCC, 0x00, 0x00, 0x00};
	return std::equal(prefix.begin(), prefix.end(),
	                  reinterpret_cast<const std::uint8_t *>(a_address - prefix.size())) &&
	       *reinterpret_cast<const std::uint8_t *>(a_address) == 0xE8 &&
	       std::equal(suffix.begin(), suffix.end(), reinterpret_cast<const std::uint8_t *>(a_address + 5));
}

[[nodiscard]] bool IsExpectedCallTarget(std::uintptr_t a_callsite, std::uintptr_t a_target) noexcept {
	std::int32_t displacement{};
	std::memcpy(&displacement, reinterpret_cast<const void *>(a_callsite + 1), sizeof(displacement));
	return static_cast<std::intptr_t>(a_callsite + 5) + displacement == static_cast<std::intptr_t>(a_target);
}

[[nodiscard]] bool IsEmbedResult(const RE::ImpactResult a_result) noexcept {
	return a_result == RE::ImpactResult::kStick || a_result == RE::ImpactResult::kImpale;
}

[[nodiscard]] bool IsExpectedQueueSubmissionCallsite(const std::uintptr_t a_address) noexcept {
	constexpr std::array<std::uint8_t, 13> prefix{0x4D, 0x8B, 0xC5, 0x49, 0x8B, 0xD7, 0x48,
	                                              0x8B, 0x0D, 0x69, 0x7F, 0xB7, 0x02};
	constexpr std::array<std::uint8_t, 5> suffix{0xE9, 0xD1, 0x10, 0x00, 0x00};
	return std::equal(prefix.begin(), prefix.end(),
	                  reinterpret_cast<const std::uint8_t *>(a_address - prefix.size())) &&
	       *reinterpret_cast<const std::uint8_t *>(a_address) == 0xE8 &&
	       std::equal(suffix.begin(), suffix.end(), reinterpret_cast<const std::uint8_t *>(a_address + 5));
}

[[nodiscard]] bool IsExpectedQueuedHitCallsite(const std::uintptr_t a_address) noexcept {
	constexpr std::array<std::uint8_t, 6> prefix{0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCE};
	constexpr std::array<std::uint8_t, 4> suffix{0x48, 0x8D, 0x4E, 0x20};
	return std::equal(prefix.begin(), prefix.end(),
	                  reinterpret_cast<const std::uint8_t *>(a_address - prefix.size())) &&
	       *reinterpret_cast<const std::uint8_t *>(a_address) == 0xE8 &&
	       std::equal(suffix.begin(), suffix.end(), reinterpret_cast<const std::uint8_t *>(a_address + 5));
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
                                                            const RE::Actor &a_target, const HitPhase a_phase,
                                                            const char **a_failure = nullptr) noexcept {
	const auto reject = [a_failure](const char *a_reason) -> RE::Projectile::ImpactData * {
		if (a_failure) {
			*a_failure = a_reason;
		}
		return nullptr;
	};
	// AddImpact installs the collision currently being handled at the list head.
	// HandleHits marks that record handled (unk48 = 1) after submitting damage.
	// Queued damage may execute after that marker changes, but before the separate
	// projectile-wide ProcessImpacts flag is set. Never scan older entries: a prior collision with the same
	// actor can otherwise be mistaken for this hit and corrupt bounce processing.
	auto &projectileData = a_projectile.GetProjectileRuntimeData();
	if (projectileData.flags.any(RE::Projectile::Flags::kProcessedImpacts) ||
	    projectileData.flags.any(RE::Projectile::Flags::kDestroyed)) {
		return reject("processed-or-destroyed");
	}
	auto &impacts = projectileData.impacts;
	if (impacts.empty()) {
		return reject("empty-impact-list");
	}
	auto *impact = impacts.front();
	if (!impact || impact->unk48 > 1 || (a_phase == HitPhase::Immediate && impact->unk48 != 0)) {
		return reject("missing-or-unexpected-handled-marker");
	}
	auto collidee = impact->collidee.get();
	if (!collidee || collidee.get() != std::addressof(a_target)) {
		return reject("current-target-mismatch");
	}
	return impact;
}

[[nodiscard]] std::optional<ImpactStamp> CaptureImpactStamp(RE::Projectile &a_projectile,
                                                            const RE::Actor &a_target, HitPhase a_phase,
                                                            const char **a_failure = nullptr) {
	const auto reject = [a_failure](const char *a_reason) -> std::optional<ImpactStamp> {
		if (a_failure) {
			*a_failure = a_reason;
		}
		return std::nullopt;
	};
	// The special explosion caller destroys the projectile even when
	// ProcessImpacts returns false; never delay that special engine path.
	if (HasUnsupportedDeferredLifecycle(a_projectile)) {
		return reject("explosion-or-chain-shatter");
	}
	auto *impact = FindCurrentImpact(a_projectile, a_target, a_phase, a_failure);
	auto *missile = a_projectile.As<RE::MissileProjectile>();
	if (!impact) {
		return std::nullopt;
	}
	if (!missile) {
		return reject("not-missile-projectile");
	}
	const auto handle = a_projectile.GetHandle().native_handle();
	if (handle == 0) {
		return reject("missing-projectile-handle");
	}
	return ImpactStamp{.projectile = handle,
	                   .target = impact->collidee.native_handle(),
	                   .impact = reinterpret_cast<std::uintptr_t>(impact),
	                   .node = reinterpret_cast<std::uintptr_t>(impact->damageRootNode),
	                   .collisionVectors = {std::bit_cast<std::uint32_t>(impact->desiredTargetLoc.x),
	                                        std::bit_cast<std::uint32_t>(impact->desiredTargetLoc.y),
	                                        std::bit_cast<std::uint32_t>(impact->desiredTargetLoc.z),
	                                        std::bit_cast<std::uint32_t>(impact->negativeVelocity.x),
	                                        std::bit_cast<std::uint32_t>(impact->negativeVelocity.y),
	                                        std::bit_cast<std::uint32_t>(impact->negativeVelocity.z)},
	                   .missileResult =
	                       static_cast<std::int32_t>(missile->GetMissileRuntimeData().impactResult),
	                   .impactResult = static_cast<std::int32_t>(impact->impactResult)};
}

void RegisterQueuedImpact(RE::Actor *a_target, RE::HitData *a_hitData) {
	if (!a_target || !a_hitData) {
		return;
	}
	if ((a_target->IsPlayerRef() ? !g_config.affectPlayer : !g_config.affectNPCs) || a_target->IsDead() ||
	    a_target->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) <= 0.0F ||
	    a_hitData->flags.any(RE::HitData::Flag::kBlocked, RE::HitData::Flag::kRicochet)) {
		return;
	}
	auto source = a_hitData->sourceRef.get();
	auto *projectile = source ? source->As<RE::ArrowProjectile>() : nullptr;
	if (!projectile || !IsLivingHumanoidTarget(CollectTargetTypeTraits(*a_target))) {
		return;
	}
	const char *failure = "none";
	if (const auto stamp = CaptureImpactStamp(*projectile, *a_target, HitPhase::QueuedCompletion, &failure)) {
		if (!IsEmbedResult(static_cast<RE::ImpactResult>(stamp->missileResult))) {
			return;
		}
		std::scoped_lock lock(g_deferredMutex);
		const bool accepted = g_deferredImpacts.Register(
		    *stamp, NowMs(), DamageDispatchScope::IsQueuedCompletionFor(a_target, a_hitData));
		if (!accepted && ClaimTelemetry(TelemetryEvent::DeferredRejected)) {
			logger::warn("runtime telemetry: ambiguous or capacity-limited queued arrow; preserving vanilla");
		}
		if (accepted && ClaimTelemetry(TelemetryEvent::QueuedImpactRegistered)) {
			logger::info("runtime telemetry: queued impact registered target={:08X} projectileFlags=0x{:08X} "
			             "hasExplosion={} missileResult={}",
			             a_target->GetFormID(), projectile->GetProjectileRuntimeData().flags.underlying(),
			             projectile->GetProjectileRuntimeData().explosion != nullptr, stamp->missileResult);
		}
	} else if (ClaimTelemetry(TelemetryEvent::QueuedRegistrationRejected)) {
		logger::warn("runtime telemetry: queued impact registration rejected target={:08X} reason={} "
		             "projectileFlags=0x{:08X} hasExplosion={}; vanilla preserved",
		             a_target->GetFormID(), failure,
		             projectile->GetProjectileRuntimeData().flags.underlying(),
		             projectile->GetProjectileRuntimeData().explosion != nullptr);
	}
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
                             const bool a_targetIsLivingHumanoid, RE::Projectile &a_projectile,
                             const HitPhase a_phase, const std::optional<ImpactStamp> &a_before) {
	if (a_projectile.GetFormType() != RE::FormType::ProjectileArrow) {
		return;
	}
	auto *missile = a_projectile.As<RE::MissileProjectile>();
	if (!missile) {
		return;
	}
	const char *failure = "none";
	const auto liveStamp = CaptureImpactStamp(a_projectile, a_target, a_phase, &failure);
	if (!a_before || !liveStamp || *a_before != *liveStamp) {
		std::scoped_lock lock(g_deferredMutex);
		g_deferredImpacts.Reject(a_projectile.GetHandle().native_handle());
		if (ClaimTelemetry(TelemetryEvent::MissingImpact)) {
			logger::warn("runtime telemetry: missing, changed, processed, or unsupported current impact; "
			             "phase={} beforePresent={} afterPresent={} afterFailure={} "
			             "projectileFlags=0x{:08X} hasExplosion={} vanilla preserved",
			             a_phase == HitPhase::QueuedCompletion ? "queued" : "immediate", a_before.has_value(),
			             liveStamp.has_value(), failure,
			             a_projectile.GetProjectileRuntimeData().flags.underlying(),
			             a_projectile.GetProjectileRuntimeData().explosion != nullptr);
		}
		return; // Callback replaced/removed/reused this impact: never mutate it.
	}

	auto *impact = FindCurrentImpact(a_projectile, a_target, a_phase);
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
	if (a_phase == HitPhase::QueuedCompletion && ClaimTelemetry(TelemetryEvent::QueuedDamageCompleted)) {
		logger::info("runtime telemetry: queued arrow damage completed target={:08X} healthRatio={:.4f} "
		             "dead={} impactHandled={} action={}",
		             a_target.GetFormID(), healthRatioAfter, engineReportsDead, impact->unk48,
		             ToString(action));
	}
	if (killedByHit && ClaimTelemetry(TelemetryEvent::LethalHitPreserved)) {
		logger::info("runtime telemetry: killing arrow preserved target={:08X} phase={} healthRatio={:.4f} "
		             "missileResult={} action={}",
		             a_target.GetFormID(),
		             a_phase == HitPhase::QueuedCompletion ? "queued-completion" : "immediate",
		             healthRatioAfter, static_cast<std::int32_t>(missileResultBefore), ToString(action));
	}
	if (ClaimTelemetry(TelemetryEvent::PolicyDecision)) {
		logger::info("runtime telemetry: first matched arrow decision target={:08X} livingHumanoid={} "
		             "region={} postHitHealthRatio={:.4f} killedByHit={} missileResult={} impactResult={} "
		             "action={}",
		             a_target.GetFormID(), a_targetIsLivingHumanoid, ToString(region), healthRatioAfter,
		             killedByHit, static_cast<std::int32_t>(missileResultBefore),
		             static_cast<std::int32_t>(impactResultBefore), ToString(action));
	}
	if (a_phase == HitPhase::QueuedCompletion) {
		std::scoped_lock lock(g_deferredMutex);
		const bool accepted = g_deferredImpacts.Complete(*liveStamp, action == ImpactAction::Bounce, NowMs());
		if (!accepted && a_targetIsLivingHumanoid && ClaimTelemetry(TelemetryEvent::DeferredRejected)) {
			logger::warn("runtime telemetry: queued decision had no current eligible pending impact; vanilla "
			             "preserved");
		}
		// Only the reviewed visual consumer may commit this stored decision.
		// It revalidates identity and waits if queued damage has not run yet.
	} else if (action == ImpactAction::Bounce) {
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

template <class Processor>
void ProcessActorHit(RE::Actor *a_target, RE::HitData &a_hitData, Processor &&a_process,
                     const HitPhase a_phase) {
	bool targetWasAlive{};
	bool targetIsLivingHumanoid{};
	RE::NiPointer<RE::TESObjectREFR> source;
	RE::ArrowProjectile *projectile{};
	std::optional<ImpactStamp> before;
	// Keep the smart pointer alive across the original hit processor; damage
	// callbacks are allowed to release references.
	const bool prepared = TryPrepareHit([&] {
		targetWasAlive = a_target && !a_target->IsDead() &&
		                 a_target->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) > 0.0F;
		source = a_hitData.sourceRef.get();
		projectile = source ? source->As<RE::ArrowProjectile>() : nullptr;
		const auto traits = a_target && projectile ? CollectTargetTypeTraits(*a_target) : TargetTypeTraits{};
		targetIsLivingHumanoid = IsLivingHumanoidTarget(traits);
		before = a_target && projectile ? CaptureImpactStamp(*projectile, *a_target, a_phase) : std::nullopt;
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
	});
	if (!prepared) {
		before.reset();
		targetIsLivingHumanoid = false;
		// The original damage callback below remains unconditional.
	}

	const bool completed = RunDamageDispatch(
	    a_target, std::addressof(a_hitData), std::forward<Processor>(a_process),
	    [&] {
		    try {
			    if (a_target && projectile) {
				    if (ClaimTelemetry(TelemetryEvent::NormalArrowReached)) {
					    logger::info(
					        "runtime telemetry: first normal arrow reached the post-damage physical-hit "
					        "hook");
				    }
				    ApplyPostDamageDecision(*a_target, a_hitData, targetWasAlive, targetIsLivingHumanoid,
				                            *projectile, a_phase, before);
			    }
		    } catch (const std::exception &error) {
			    TryPrepareHit([&] {
				    if (ClaimTelemetry(TelemetryEvent::HandlerFailure)) {
					    logger::error(
					        "post-damage arrow decision failed safely; further identical errors are "
					        "suppressed: {}",
					        error.what());
				    }
			    });
		    } catch (...) {
			    TryPrepareHit([&] {
				    if (ClaimTelemetry(TelemetryEvent::HandlerFailure)) {
					    logger::error(
					        "post-damage arrow decision failed safely with an unknown exception; further "
					        "identical errors are suppressed");
				    }
			    });
		    }
	    },
	    a_phase == HitPhase::QueuedCompletion);
	TryPrepareHit([&] {
		if (!completed && projectile && ClaimTelemetry(TelemetryEvent::DamageDeferred)) {
			logger::info("runtime telemetry: arrow damage queued; no embedding decision made before damage "
			             "completion target={:08X}",
			             a_target ? a_target->GetFormID() : 0);
		}
	});
}

struct ProjectileActorHitHook {
	static void Thunk(RE::Actor *a_target, RE::HitData &a_hitData) {
		ProcessActorHit(a_target, a_hitData, [&] { Func(a_target, a_hitData); }, HitPhase::Immediate);
	}
	static inline REL::Relocation<decltype(Thunk)> Func;
};

struct QueueSubmissionHook {
	static void Thunk(void *a_queue, RE::Actor *a_target, RE::HitData *a_hitData) {
		DamageDispatchScope::MarkDeferred(a_target, a_hitData);
		try {
			RegisterQueuedImpact(a_target, a_hitData);
		} catch (...) {
			// Nothing, including an allocation failure in diagnostics, may escape
			// between queue bookkeeping and the original damage submission.
		}
		Func(a_queue, a_target, a_hitData);
	}
	static inline REL::Relocation<decltype(Thunk)> Func;
};

struct ArrowProcessImpactsHook {
	static bool Thunk(RE::ArrowProjectile *a_projectile) {
		try {
			auto &data = a_projectile->GetProjectileRuntimeData();
			auto *impact = data.impacts.empty() ? nullptr : data.impacts.front();
			auto target = impact ? impact->collidee.get() : RE::NiPointer<RE::TESObjectREFR>{};
			auto *actor = target ? target->As<RE::Actor>() : nullptr;
			const auto stamp =
			    actor ? CaptureImpactStamp(*a_projectile, *actor, HitPhase::QueuedCompletion) : std::nullopt;
			VisualGateAction action;
			{
				std::scoped_lock lock(g_deferredMutex);
				action = g_deferredImpacts.Consume(a_projectile->GetHandle().native_handle(), stamp, NowMs(),
				                                   impact && impact->unk48 == 1);
			}
			if (action == VisualGateAction::Wait) {
				if (ClaimTelemetry(TelemetryEvent::VisualDeferred)) {
					logger::info("runtime telemetry: visual impact deferred until queued damage completes");
				}
				return false; // Reviewed normal update exits without consuming or clearing the impact.
			}
			if (action == VisualGateAction::Bounce && impact && stamp) {
				a_projectile->GetMissileRuntimeData().impactResult = RE::ImpactResult::kBounce;
				impact->impactResult = RE::ImpactResult::kBounce;
				if (ClaimTelemetry(TelemetryEvent::VisualBounceApplied)) {
					logger::info("runtime telemetry: queued post-damage bounce committed at visual consumer");
				}
			}
		} catch (...) {
			TryPrepareHit([&] {
				if (ClaimTelemetry(TelemetryEvent::HandlerFailure)) {
					logger::error("arrow visual gate failed safely; preserving original impact processing");
				}
			});
		}
		return Func(a_projectile);
	}
	static inline REL::Relocation<decltype(Thunk)> Func;
};

struct QueuedActorHitHook {
	static void Thunk(RE::Actor *a_target, RE::HitData &a_hitData) {
		ProcessActorHit(a_target, a_hitData, [&] { Func(a_target, a_hitData); }, HitPhase::QueuedCompletion);
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
	const REL::Relocation<std::uintptr_t> actorProcessHit{REL::ID(ActorProcessHitAddressId)};
	const REL::Relocation<std::uintptr_t> queuedCommandRunner{REL::ID(QueuedCommandRunnerAddressId)};
	const auto submissionCallsite = actorProcessHit.address() + QueueSubmissionCallOffset;
	const auto completionCallsite = queuedCommandRunner.address() + QueuedActorHitCallOffset;
	REL::Relocation<std::uintptr_t> arrowVtable{REL::ID(ArrowVtableAddressId)};
	const auto visualConsumer =
	    reinterpret_cast<const std::uintptr_t *>(arrowVtable.address())[ProcessImpactsVtableIndex];
	const auto imageBase = REL::Module::get().base();
	// Vtable hooks cannot be safely chained without auditing the other consumer:
	// refuse foreign/replaced targets and mismatched Address Library mappings.
	if (physicalHitDispatcher.address() - imageBase != 0x6CD2F0 ||
	    actorProcessHit.address() - imageBase != 0x6CA640 ||
	    queuedCommandRunner.address() - imageBase != 0x669920 ||
	    arrowVtable.address() - imageBase != 0x193C248 || visualConsumer - imageBase != 0x7DDEC0) {
		logger::critical("reviewed runtime RVAs or arrow visual consumer changed; no arrow hooks installed");
		return false;
	}
	constexpr std::array<std::uint8_t, 12> visualPrefix{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
	                                                    0x8B, 0x81, 0xD4, 0x01, 0x00, 0x00};
	if (!std::equal(visualPrefix.begin(), visualPrefix.end(),
	                reinterpret_cast<const std::uint8_t *>(visualConsumer))) {
		logger::critical("arrow visual consumer signature mismatch; no arrow hooks installed");
		return false;
	}
	if (!IsExpectedPhysicalHitCallsite(callsite)) {
		logger::critical("physical-hit callsite signature mismatch at 0x{:X}; hook not installed", callsite);
		return false;
	}
	if (!IsExpectedQueueSubmissionCallsite(submissionCallsite) ||
	    !IsExpectedQueuedHitCallsite(completionCallsite)) {
		logger::critical("queued-damage callsite signature mismatch; no arrow hooks installed");
		return false;
	}
	if (!IsExpectedCallTarget(callsite, actorProcessHit.address()) ||
	    !IsExpectedCallTarget(completionCallsite, actorProcessHit.address()) ||
	    !IsExpectedCallTarget(submissionCallsite, imageBase + 0x667D80)) {
		logger::critical("reviewed native damage call target was replaced; no arrow hooks installed");
		return false;
	}

	SKSE::AllocTrampoline(128);
	auto &trampoline = SKSE::GetTrampoline();
	QueueSubmissionHook::Func = trampoline.write_call<5>(submissionCallsite, QueueSubmissionHook::Thunk);
	QueuedActorHitHook::Func = trampoline.write_call<5>(completionCallsite, QueuedActorHitHook::Thunk);
	ProjectileActorHitHook::Func = trampoline.write_call<5>(callsite, ProjectileActorHitHook::Thunk);
	ArrowProcessImpactsHook::Func =
	    arrowVtable.write_vfunc(ProcessImpactsVtableIndex, ArrowProcessImpactsHook::Thunk);
	logger::info("queue-aware physical-hit hooks installed at ID {} + 0x{:X}, "
	             "38586 + 0xA7 (queue submission), 36991 + 0xA3D (queued completion); body threshold={:.3f}; "
	             "arrow visual-consumer gate installed at vtable 209891[0xAC]; "
	             "default runtime telemetry is bounded to fourteen first-occurrence messages",
	             PhysicalHitDispatcherAddressId, PhysicalHitCallOffset, g_config.bodyStickBelowHealthRatio);
	return true;
}
} // namespace ConditionalArrowEmbedding::Hooks
