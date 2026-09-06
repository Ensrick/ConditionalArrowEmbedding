#include "Policy.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ConditionalArrowEmbedding {
namespace {
inline constexpr std::int32_t LimbNone = -1;
inline constexpr std::int32_t LimbTorso = 0;
inline constexpr std::int32_t LimbHead = 1;
inline constexpr std::int32_t LimbEye = 2;
inline constexpr std::int32_t LimbLookAt = 3;
inline constexpr std::int32_t LimbFlyGrab = 4;
inline constexpr std::int32_t LimbSaddle = 5;

[[nodiscard]] bool EqualsCaseInsensitiveAt(std::string_view a_value, std::string_view a_token,
                                           const std::size_t a_offset) noexcept {
	for (std::size_t index = 0; index < a_token.size(); ++index) {
		const auto left = static_cast<unsigned char>(a_value[a_offset + index]);
		const auto right = static_cast<unsigned char>(a_token[index]);
		if (std::tolower(left) != std::tolower(right)) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view a_value, std::string_view a_token) noexcept {
	if (a_token.empty() || a_token.size() > a_value.size()) {
		return false;
	}
	for (std::size_t offset = 0; offset + a_token.size() <= a_value.size(); ++offset) {
		if (EqualsCaseInsensitiveAt(a_value, a_token, offset)) {
			return true;
		}
	}
	return false;
}
} // namespace

bool IsLivingHumanoidTarget(const TargetTypeTraits &a_traits) noexcept {
	const bool hasExcludedType = a_traits.actorTypeAnimal || a_traits.actorTypeCreature ||
	                             a_traits.actorTypeDaedra || a_traits.actorTypeDragon ||
	                             a_traits.actorTypeDwarven || a_traits.actorTypeGhost ||
	                             a_traits.actorTypeUndead || a_traits.ghost || a_traits.reanimated ||
	                             a_traits.excludedVanillaUtilityRace;
	return a_traits.hasRace && a_traits.actorTypeNPC && !hasExcludedType;
}

bool WasKilledByHit(const PostDamageState &a_state) noexcept {
	return a_state.targetWasAlive && a_state.targetReportsDead;
}

ImpactAction DecideImpact(const ImpactContext &a_context) noexcept {
	if (!a_context.enabled || !a_context.vanillaWouldEmbed || a_context.blockedOrAlreadyRicocheting ||
	    !a_context.targetWasAlive || !a_context.targetIsLivingHumanoid) {
		return ImpactAction::PreserveVanilla;
	}
	if ((a_context.targetIsPlayer && !a_context.affectPlayer) ||
	    (!a_context.targetIsPlayer && !a_context.affectNPCs)) {
		return ImpactAction::PreserveVanilla;
	}
	if (a_context.targetKilledByHit) {
		return ImpactAction::PreserveVanilla;
	}
	if (a_context.region == HitRegion::Head) {
		return ImpactAction::Bounce;
	}
	if (!std::isfinite(a_context.healthRatioAfter)) {
		return ImpactAction::PreserveVanilla;
	}
	// Above the body threshold, both head and body rules select bounce. Missing
	// limb/node metadata must not make a healthy living humanoid retain arrows.
	// Below it, unknown locations remain vanilla because a head hit is unproven.
	return a_context.healthRatioAfter >= a_context.bodyStickBelowHealthRatio ? ImpactAction::Bounce
	                                                                         : ImpactAction::PreserveVanilla;
}

HitRegion ClassifyHitRegion(const std::int32_t a_engineLimb,
                            const std::span<const std::string> a_nodeAndAncestorNames,
                            const std::span<const std::string> a_headTokens) noexcept {
	if (a_engineLimb == LimbHead || a_engineLimb == LimbEye) {
		return HitRegion::Head;
	}
	for (const auto &name : a_nodeAndAncestorNames) {
		for (const auto &token : a_headTokens) {
			if (ContainsCaseInsensitive(name, token)) {
				return HitRegion::Head;
			}
		}
	}
	if (a_engineLimb == LimbTorso || a_engineLimb == LimbLookAt || a_engineLimb == LimbFlyGrab ||
	    a_engineLimb == LimbSaddle || !a_nodeAndAncestorNames.empty()) {
		return HitRegion::Body;
	}
	if (a_engineLimb == LimbNone) {
		return HitRegion::Unknown;
	}
	return HitRegion::Unknown;
}

std::string_view ToString(const HitRegion a_region) noexcept {
	switch (a_region) {
	case HitRegion::Head:
		return "head";
	case HitRegion::Body:
		return "body";
	case HitRegion::Unknown:
	default:
		return "unknown";
	}
}

std::string_view ToString(const ImpactAction a_action) noexcept {
	return a_action == ImpactAction::Bounce ? "bounce" : "preserve-vanilla";
}
} // namespace ConditionalArrowEmbedding
