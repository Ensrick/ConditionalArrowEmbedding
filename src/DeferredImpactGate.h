#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace ConditionalArrowEmbedding {
// Only identity values are retained between engine callbacks. These pointers
// are compared with live, handle-resolved data; they are never dereferenced.
struct ImpactStamp {
	std::uint32_t projectile{};
	std::uint32_t target{};
	std::uintptr_t impact{};
	std::uintptr_t node{};
	std::array<std::uint32_t, 6> collisionVectors{};
	std::int32_t missileResult{};
	std::int32_t impactResult{};
	bool operator==(const ImpactStamp &) const = default;
};

enum class VisualGateAction { Vanilla, Wait, Bounce };

// Callers serialize access. Fixed capacity and deadline prevent retained state
// from growing or blocking visuals indefinitely when another plugin discards a
// queued hit. Rejected/ambiguous hits preserve vanilla, never guessed damage.
class DeferredImpactGate {
  public:
	static constexpr std::size_t Capacity = 256;
	static constexpr std::uint64_t MaximumWaitMs = 2000;

	bool Register(const ImpactStamp &a_stamp, std::uint64_t a_now, bool a_requeue = false) noexcept {
		Expire(a_now);
		if (auto *entry = Find(a_stamp.projectile)) {
			if (a_requeue && entry->phase == Phase::Waiting && entry->stamp == a_stamp) {
				return true; // The consumer deferred the same hit again; original deadline stands.
			}
			entry->phase = Phase::Rejected;
			return false;
		}
		for (auto &entry : entries) {
			if (!entry) {
				entry = Entry{a_stamp, a_now, Phase::Waiting};
				return true;
			}
		}
		return false;
	}

	bool Complete(const ImpactStamp &a_stamp, bool a_bounce, std::uint64_t a_now) noexcept {
		Expire(a_now);
		if (auto *entry = Find(a_stamp.projectile)) {
			if (entry->phase == Phase::Waiting && entry->stamp == a_stamp) {
				entry->phase = a_bounce ? Phase::Bounce : Phase::Vanilla;
				return true;
			}
			entry->phase = Phase::Rejected;
		}
		return false;
	}

	VisualGateAction Consume(std::uint32_t a_projectile, const std::optional<ImpactStamp> &a_live,
	                         std::uint64_t a_now, bool a_submissionHandled = true) noexcept {
		Expire(a_now);
		for (auto &entry : entries) {
			if (!entry || entry->stamp.projectile != a_projectile) {
				continue;
			}
			if (!a_live || entry->stamp != *a_live || entry->phase == Phase::Rejected) {
				entry.reset();
				return VisualGateAction::Vanilla;
			}
			if (entry->phase == Phase::Waiting || !a_submissionHandled) {
				return VisualGateAction::Wait;
			}
			const auto action =
			    entry->phase == Phase::Bounce ? VisualGateAction::Bounce : VisualGateAction::Vanilla;
			entry.reset();
			return action;
		}
		return VisualGateAction::Vanilla;
	}

	void Reject(std::uint32_t a_projectile) noexcept {
		if (auto *entry = Find(a_projectile)) {
			entry->phase = Phase::Rejected;
		}
	}

  private:
	enum class Phase { Waiting, Bounce, Vanilla, Rejected };
	struct Entry {
		ImpactStamp stamp;
		std::uint64_t started;
		Phase phase;
	};
	std::array<std::optional<Entry>, Capacity> entries{};
	Entry *Find(std::uint32_t a_projectile) noexcept {
		for (auto &entry : entries) {
			if (entry && entry->stamp.projectile == a_projectile) {
				return &*entry;
			}
		}
		return nullptr;
	}
	void Expire(std::uint64_t a_now) noexcept {
		for (auto &entry : entries) {
			if (entry && (a_now < entry->started || a_now - entry->started >= MaximumWaitMs)) {
				entry.reset();
			}
		}
	}
};
} // namespace ConditionalArrowEmbedding
