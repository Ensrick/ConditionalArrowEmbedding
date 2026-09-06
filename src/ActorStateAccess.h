#pragma once

#include "RE/A/Actor.h"

namespace ConditionalArrowEmbedding {
[[nodiscard]] inline bool IsRuntimeReanimated(const RE::Actor &a_target) noexcept {
	// Actor's inherited C++ base-class offset is not its engine offset in a
	// multi-runtime build. On 1.7.104 the state is at Actor+0xC0, not the
	// compiled ActorState base; reading the latter interprets unrelated memory.
	return a_target.AsActorState()->IsReanimated();
}
} // namespace ConditionalArrowEmbedding
