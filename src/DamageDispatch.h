#pragma once

#include <utility>

namespace ConditionalArrowEmbedding {
template <class Preparation> bool TryPrepareHit(Preparation &&a_prepare) noexcept {
	try {
		std::forward<Preparation>(a_prepare)();
		return true;
	} catch (...) {
		return false;
	}
}
// The engine can copy and enqueue HitData rather than apply it on this call.
// A thread-local stack recognizes that exact branch without guessing from
// damage, health, fatal flags, or whether the player has godmode enabled.
class DamageDispatchScope {
  public:
	DamageDispatchScope(const void *a_actor, const void *a_hit, bool a_queuedCompletion = false) noexcept
	    : actor(a_actor), hit(a_hit), previous(current), queuedCompletion(a_queuedCompletion) {
		current = this;
	}
	~DamageDispatchScope() { current = previous; }
	DamageDispatchScope(const DamageDispatchScope &) = delete;
	DamageDispatchScope &operator=(const DamageDispatchScope &) = delete;
	[[nodiscard]] bool IsDeferred() const noexcept { return deferred; }
	static bool IsQueuedCompletionFor(const void *a_actor, const void *a_hit) noexcept {
		for (auto *scope = current; scope; scope = scope->previous) {
			if (scope->actor == a_actor && scope->hit == a_hit) {
				return scope->queuedCompletion;
			}
		}
		return false;
	}
	static void MarkDeferred(const void *a_actor, const void *a_hit) noexcept {
		for (auto *scope = current; scope; scope = scope->previous) {
			if (scope->actor == a_actor && scope->hit == a_hit) {
				scope->deferred = true;
				return;
			}
		}
	}

  private:
	const void *actor;
	const void *hit;
	DamageDispatchScope *previous;
	bool deferred{false};
	bool queuedCompletion;
	static inline thread_local DamageDispatchScope *current{};
};

template <class Processor, class Completion>
bool RunDamageDispatch(const void *a_actor, const void *a_hit, Processor &&a_process, Completion &&a_complete,
                       bool a_queuedCompletion = false) {
	DamageDispatchScope scope(a_actor, a_hit, a_queuedCompletion);
	std::forward<Processor>(a_process)();
	if (scope.IsDeferred()) {
		return false;
	}
	std::forward<Completion>(a_complete)();
	return true;
}
} // namespace ConditionalArrowEmbedding
