#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ConditionalArrowEmbedding {
enum class HitRegion : std::uint8_t { Unknown, Head, Body };

enum class ImpactAction : std::uint8_t { PreserveVanilla, Bounce };

struct ImpactContext {
	bool enabled{true};
	bool targetWasAlive{true};
	bool targetKilledByHit{false};
	bool vanillaWouldEmbed{true};
	bool blockedOrAlreadyRicocheting{false};
	bool targetIsPlayer{false};
	bool affectPlayer{true};
	bool affectNPCs{true};
	HitRegion region{HitRegion::Unknown};
	double healthRatioAfter{1.0};
	double bodyStickBelowHealthRatio{0.5};
};

[[nodiscard]] ImpactAction DecideImpact(const ImpactContext &a_context) noexcept;
[[nodiscard]] HitRegion ClassifyHitRegion(std::int32_t a_engineLimb,
                                          std::span<const std::string> a_nodeAndAncestorNames,
                                          std::span<const std::string> a_headTokens) noexcept;
[[nodiscard]] std::string_view ToString(HitRegion a_region) noexcept;
[[nodiscard]] std::string_view ToString(ImpactAction a_action) noexcept;
} // namespace ConditionalArrowEmbedding
