#pragma once

#include "RE/P/Projectile.h"

namespace ConditionalArrowEmbedding {
[[nodiscard]] inline bool HasUnsupportedDeferredLifecycle(const RE::Projectile &a_projectile) noexcept {
	const auto &data = a_projectile.GetProjectileRuntimeData();
	// On 1.7.104, Handle3DLoaded sets kDestroyAfterHit for ordinary non-hitscan
	// arrows too. It is NOT proof of the special caller that ignores a false
	// ProcessImpacts return. That caller (802580 -> 8029DA) requires an actual
	// runtime explosion pointer; exclude that lifecycle and chain-shatter only.
	return data.explosion != nullptr || data.flags.any(RE::Projectile::Flags::kChainShatter);
}
} // namespace ConditionalArrowEmbedding
