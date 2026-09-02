#include "PCH.h"

#include "Hooks.h"
#include "Policy.h"

namespace ConditionalArrowEmbedding::Hooks {
namespace {
Config g_config{};

[[nodiscard]] bool IsExpectedProjectileHitCallsite(const std::uintptr_t a_address) noexcept {
	// SkyrimSE 1.7.104, MissileProjectile::ProcessImpacts (Address Library
	// ID 44204), immediately around the call that applies an arrow HitData
	// to the collided Character. Refuse to patch if another runtime or mod
	// changed the instruction boundary.
	constexpr std::array<std::uint8_t, 7> prefix{0x48, 0x8D, 0x55, 0xC0, 0x48, 0x8B, 0xCE};
	constexpr std::array<std::uint8_t, 5> suffix{0x90, 0x48, 0x8D, 0x4D, 0xC0};
	return std::equal(prefix.begin(), prefix.end(),
	                  reinterpret_cast<const std::uint8_t *>(a_address - prefix.size())) &&
	       *reinterpret_cast<const std::uint8_t *>(a_address) == 0xE8 &&
	       std::equal(suffix.begin(), suffix.end(), reinterpret_cast<const std::uint8_t *>(a_address + 5));
}

[[nodiscard]] bool IsEmbedResult(const RE::ImpactResult a_result) noexcept {
	return a_result == RE::ImpactResult::kStick || a_result == RE::ImpactResult::kImpale;
}

[[nodiscard]] float DistanceSquared(const RE::NiPoint3 &a_left, const RE::NiPoint3 &a_right) noexcept {
	const auto x = a_left.x - a_right.x;
	const auto y = a_left.y - a_right.y;
	const auto z = a_left.z - a_right.z;
	return x * x + y * y + z * z;
}

[[nodiscard]] RE::Projectile::ImpactData *FindImpact(RE::Projectile &a_projectile, const RE::Actor &a_target,
                                                     const RE::NiPoint3 &a_hitPosition) noexcept {
	RE::Projectile::ImpactData *targetFallback = nullptr;
	for (auto *impact : a_projectile.GetProjectileRuntimeData().impacts) {
		if (!impact) {
			continue;
		}
		auto collidee = impact->collidee.get();
		if (!collidee || collidee.get() != std::addressof(a_target)) {
			continue;
		}
		if (!targetFallback) {
			targetFallback = impact;
		}
		if (DistanceSquared(impact->desiredTargetLoc, a_hitPosition) <= 0.01F) {
			return impact;
		}
	}
	return targetFallback;
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

void ApplyPostDamageDecision(RE::Character &a_target, RE::HitData &a_hitData, const bool a_targetWasAlive,
                             RE::Projectile &a_projectile) {
	if (a_projectile.GetFormType() != RE::FormType::ProjectileArrow) {
		return;
	}
	auto *missile = a_projectile.As<RE::MissileProjectile>();
	if (!missile) {
		return;
	}

	auto *impact = FindImpact(a_projectile, a_target, a_hitData.hitPosition);
	if (!impact) {
		if (g_config.debugLogging) {
			logger::debug("arrow {:08X}: no matching actor impact; preserving vanilla",
			              a_projectile.GetFormID());
		}
		return;
	}

	auto &missileData = missile->GetMissileRuntimeData();
	const bool vanillaWouldEmbed =
	    IsEmbedResult(missileData.impactResult) || IsEmbedResult(impact->impactResult);
	const bool blockedOrRicocheting =
	    a_hitData.flags.any(RE::HitData::Flag::kBlocked) || a_hitData.flags.any(RE::HitData::Flag::kRicochet);
	const bool killedByHit =
	    a_targetWasAlive && (a_hitData.flags.any(RE::HitData::Flag::kFatal) || a_target.IsDead());

	const auto names = CollectNodeNames(impact, g_config.fallbackHeadAncestorDepth);
	const auto region = ClassifyHitRegion(static_cast<std::int32_t>(a_hitData.damageLimb.underlying()), names,
	                                      g_config.fallbackHeadNodeTokens);

	const auto healthAfter =
	    static_cast<double>(a_target.AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth));
	const auto maximumHealth = static_cast<double>(a_target.GetActorValueMax(RE::ActorValue::kHealth));
	const auto healthRatioAfter = maximumHealth > 0.0 && std::isfinite(maximumHealth)
	                                  ? std::clamp(healthAfter / maximumHealth, 0.0, 1.0)
	                                  : std::numeric_limits<double>::quiet_NaN();

	const ImpactContext context{.enabled = g_config.enabled,
	                            .targetWasAlive = a_targetWasAlive,
	                            .targetKilledByHit = killedByHit,
	                            .vanillaWouldEmbed = vanillaWouldEmbed,
	                            .blockedOrAlreadyRicocheting = blockedOrRicocheting,
	                            .targetIsPlayer = a_target.IsPlayerRef(),
	                            .affectPlayer = g_config.affectPlayer,
	                            .affectNPCs = g_config.affectNPCs,
	                            .region = region,
	                            .healthRatioAfter = healthRatioAfter,
	                            .bodyStickBelowHealthRatio = g_config.bodyStickBelowHealthRatio};
	const auto action = DecideImpact(context);
	if (action == ImpactAction::Bounce) {
		missileData.impactResult = RE::ImpactResult::kBounce;
		impact->impactResult = RE::ImpactResult::kBounce;
	}

	if (g_config.debugLogging) {
		logger::debug("arrow {:08X} target {:08X}: region={} healthAfter={:.4f} killedByHit={} "
		              "vanillaEmbed={} action={}",
		              a_projectile.GetFormID(), a_target.GetFormID(), ToString(region), healthRatioAfter,
		              killedByHit, vanillaWouldEmbed, ToString(action));
	}
}

struct ProjectileActorHitHook {
	static void *Thunk(RE::Character *a_target, RE::HitData *a_hitData) {
		const bool targetWasAlive =
		    a_target && !a_target->IsDead() &&
		    a_target->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) > 0.0F;
		auto source = a_hitData ? a_hitData->sourceRef.get() : RE::NiPointer<RE::TESObjectREFR>{};
		auto *projectile = source ? source->As<RE::Projectile>() : nullptr;

		void *result = Func(a_target, a_hitData);
		try {
			if (a_target && a_hitData && projectile) {
				ApplyPostDamageDecision(*a_target, *a_hitData, targetWasAlive, *projectile);
			}
		} catch (const std::exception &error) {
			logger::error("post-damage arrow decision failed safely: {}", error.what());
		} catch (...) {
			logger::error("post-damage arrow decision failed safely with an unknown exception");
		}
		return result;
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

	// Verified against SkyrimSE.exe 1.7.104.0. Address Library ID 44204
	// resolves MissileProjectile::ProcessImpacts; +0x3AA is the only direct
	// call that commits its prepared HitData to the collided Character.
	const REL::Relocation<std::uintptr_t> processImpacts{REL::ID(44204)};
	const auto callsite = processImpacts.address() + 0x3AA;
	if (!IsExpectedProjectileHitCallsite(callsite)) {
		logger::critical("projectile hit callsite signature mismatch at 0x{:X}; hook not installed",
		                 callsite);
		return false;
	}

	SKSE::AllocTrampoline(64);
	auto &trampoline = SKSE::GetTrampoline();
	ProjectileActorHitHook::Func = trampoline.write_call<5>(callsite, ProjectileActorHitHook::Thunk);
	logger::info("post-damage projectile hook installed at ID 44204 + 0x3AA; body threshold={:.3f}",
	             g_config.bodyStickBelowHealthRatio);
	return true;
}
} // namespace ConditionalArrowEmbedding::Hooks
