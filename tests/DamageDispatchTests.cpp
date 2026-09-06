#include "DamageDispatch.h"
#include "DeferredImpactGate.h"
#include "Policy.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace ConditionalArrowEmbedding;
namespace {
int failures{};
void Expect(bool a_result, const char *a_name) {
	if (!a_result) {
		std::cerr << "FAILED: " << a_name << '\n';
		++failures;
	}
}
ImpactStamp Stamp(std::uint32_t a_projectile = 1) {
	return {.projectile = a_projectile,
	        .target = 2,
	        .impact = 3,
	        .node = 4,
	        .collisionVectors = {5, 6, 7, 8, 9, 10},
	        .missileResult = 4,
	        .impactResult = 0};
}
} // namespace

int main() {
	int actor{}, hit{}, copiedHit{};
	int originalCalls{}, completionCalls{};
	RunDamageDispatch(&actor, &hit, [&] { ++originalCalls; }, [&] { ++completionCalls; });
	Expect(originalCalls == 1 && completionCalls == 1, "synchronous original and completion exactly once");
	originalCalls = completionCalls = 0;
	const bool completed = RunDamageDispatch(
	    &actor, &hit,
	    [&] {
		    ++originalCalls;
		    DamageDispatchScope::MarkDeferred(&actor, &hit);
	    },
	    [&] { ++completionCalls; });
	Expect(!completed && originalCalls == 1 && completionCalls == 0,
	       "queue submission never makes an early health/death decision");
	RunDamageDispatch(
	    &actor, &hit,
	    [&] {
		    RunDamageDispatch(
		        &actor, &copiedHit, [&] { DamageDispatchScope::MarkDeferred(&actor, &hit); },
		        [&] { ++completionCalls; });
	    },
	    [&] { completionCalls += 100; });
	Expect(completionCalls == 1, "nested unrelated hit cannot mask outer queue submission");
	RunDamageDispatch(
	    &actor, &hit,
	    [&] {
		    std::thread other([&] { DamageDispatchScope::MarkDeferred(&actor, &hit); });
		    other.join();
	    },
	    [&] { ++completionCalls; });
	Expect(completionCalls == 2, "thread-local scopes cannot mark another thread's hit deferred");
	try {
		volatile bool shouldThrow = true;
		RunDamageDispatch(
		    &actor, &hit,
		    [&] {
			    if (shouldThrow) {
				    throw std::runtime_error("test");
			    }
		    },
		    [] {});
	} catch (const std::runtime_error &) {
	}
	Expect(RunDamageDispatch(&actor, &hit, [] {}, [] {}), "scope restores after processor exception");
	{
		const bool prepared = TryPrepareHit([] { throw std::runtime_error("pre-capture failure"); });
		int calls{};
		RunDamageDispatch(
		    &actor, &hit, [&] { ++calls; },
		    [&] { Expect(!prepared, "failed pre-capture is not used by policy"); });
		Expect(calls == 1, "pre-capture exception cannot prevent original damage");
	}

	// Exercise the real dispatch helper, gate and production policy together.
	// These are native host tests of callback ordering, NOT a simulated Skyrim
	// damage engine: health/death are supplied by the fixture after its callback.
	for (const auto region : {HitRegion::Head, HitRegion::Body, HitRegion::Unknown}) {
		for (const bool lethal : {false, true}) {
			for (const bool consumerFirst : {false, true}) {
				DeferredImpactGate gate;
				const auto stamp = Stamp();
				double health = 1.0;
				bool dead = false;
				int damageApplications{};
				int decisions{};
				RunDamageDispatch(
				    &actor, &hit,
				    [&] {
					    Expect(gate.Register(stamp, 0), "register before engine queues copied HitData");
					    DamageDispatchScope::MarkDeferred(&actor, &hit);
				    },
				    [&] { ++decisions; });
				Expect(decisions == 0 && health == 1.0, "pre-hit health does not drive queued policy");
				if (consumerFirst) {
					Expect(gate.Consume(stamp.projectile, stamp, 1) == VisualGateAction::Wait,
					       "consumer before damage cannot consume embed/bounce");
				}
				RunDamageDispatch(
				    &actor, &copiedHit,
				    [&] {
					    ++damageApplications;
					    dead = lethal;
					    health = lethal ? 0.0 : 0.75;
				    },
				    [&] {
					    ++decisions;
					    ImpactContext context{.targetKilledByHit = WasKilledByHit({true, dead}),
					                          .region = region,
					                          .healthRatioAfter = health};
					    Expect(gate.Complete(stamp, DecideImpact(context) == ImpactAction::Bounce, 2),
					           "queued damage decision stored, not visually consumed yet");
				    },
				    true);
				Expect(damageApplications == 1 && decisions == 1,
				       "damage and actual-post-hit decision each once");
				Expect(gate.Consume(stamp.projectile, stamp, 3) ==
				           (lethal ? VisualGateAction::Vanilla : VisualGateAction::Bounce),
				       "all above-half-health killing hits preserve vanilla; survivors bounce");
				Expect(gate.Consume(stamp.projectile, stamp, 4) == VisualGateAction::Vanilla,
				       "decision cannot replay after visual consumption");
			}
		}
	}
	for (const auto health : {1.0, 0.5, 0.499, 0.0}) {
		for (const auto region : {HitRegion::Head, HitRegion::Body, HitRegion::Unknown}) {
			for (const bool humanoid : {false, true}) {
				DeferredImpactGate gate;
				auto stamp = Stamp();
				gate.Register(stamp, 0);
				ImpactContext context{
				    .targetIsLivingHumanoid = humanoid, .region = region, .healthRatioAfter = health};
				const auto expected = humanoid && (health >= 0.5 || region == HitRegion::Head)
				                          ? VisualGateAction::Bounce
				                          : VisualGateAction::Vanilla;
				gate.Complete(stamp, DecideImpact(context) == ImpactAction::Bounce, 1);
				Expect(gate.Consume(stamp.projectile, stamp, 2) == expected,
				       "strict threshold, godmode health, creature exclusion and unknown-location rules "
				       "persist");
			}
		}
	}
	{
		DeferredImpactGate gate;
		const auto stamp = Stamp();
		gate.Register(stamp, 0);
		RunDamageDispatch(
		    &actor, &copiedHit,
		    [&] {
			    Expect(DamageDispatchScope::IsQueuedCompletionFor(&actor, &copiedHit),
			           "requeue scope classified");
			    Expect(gate.Register(stamp, 20, true), "actual requeue retains original pending identity");
			    DamageDispatchScope::MarkDeferred(&actor, &copiedHit);
		    },
		    [&] { Expect(false, "requeued hit must not complete"); }, true);
		Expect(gate.Consume(stamp.projectile, stamp, 21) == VisualGateAction::Wait,
		       "requeued damage still gates visuals");
		Expect(gate.Consume(stamp.projectile, stamp, DeferredImpactGate::MaximumWaitMs) ==
		           VisualGateAction::Vanilla,
		       "requeue cannot extend deadline indefinitely");
		Expect(!gate.Complete(stamp, true, 2001), "late completion after timeout cannot mutate visuals");
	}
	{
		DeferredImpactGate gate;
		const auto stamp = Stamp();
		gate.Register(stamp, 0);
		Expect(!gate.Register(stamp, 1), "duplicate pending hit is ambiguous, not overwritten");
		Expect(!gate.Complete(stamp, true, 2), "ambiguous completion cannot bounce");
		Expect(gate.Consume(stamp.projectile, stamp, 3) == VisualGateAction::Vanilla,
		       "ambiguous hits fail open");
		gate.Register(stamp, 4);
		gate.Reject(stamp.projectile);
		Expect(gate.Consume(stamp.projectile, stamp, 5) == VisualGateAction::Vanilla,
		       "explicit callback invalidation");
	}
	{
		DeferredImpactGate gate;
		const auto stamp = Stamp();
		gate.Register(stamp, 0);
		gate.Complete(stamp, true, 1);
		Expect(gate.Consume(stamp.projectile, stamp, 2, false) == VisualGateAction::Wait,
		       "completion before HandleHits marks submission cannot replay impact damage");
		Expect(gate.Consume(stamp.projectile, stamp, 3, true) == VisualGateAction::Bounce,
		       "visual consumption resumes only after native handled marker is present");
	}
	// Independently poison every identity component: no stale pointer is ever
	// dereferenced, and changed list heads/targets/results cannot inherit a bounce.
	for (int field = 0; field < 12; ++field) {
		DeferredImpactGate gate;
		const auto stamp = Stamp();
		gate.Register(stamp, 0);
		gate.Complete(stamp, true, 1);
		auto changed = stamp;
		if (field == 0) {
			++changed.target;
		} else if (field == 1) {
			++changed.impact;
		} else if (field == 2) {
			++changed.node;
		} else if (field == 3) {
			++changed.missileResult;
		} else if (field == 4) {
			++changed.impactResult;
		} else if (field == 5) {
			++changed.projectile;
		} else {
			++changed.collisionVectors[static_cast<std::size_t>(field - 6)];
		}
		Expect(gate.Consume(stamp.projectile, changed, 2) == VisualGateAction::Vanilla,
		       "changed impact identity rejects stale decision");
	}
	{
		DeferredImpactGate gate;
		gate.Register(Stamp(), 0);
		Expect(gate.Consume(1, std::nullopt, 1) == VisualGateAction::Vanilla,
		       "destroyed or removed live impact fails open");
		for (std::uint32_t i = 1; i <= DeferredImpactGate::Capacity; ++i) {
			Expect(gate.Register(Stamp(i), 2), "fixed-capacity entry accepted");
		}
		Expect(!gate.Register(Stamp(1000), 3), "capacity overflow fails open without growth");
		Expect(gate.Register(Stamp(1000), 2002), "expired canceled queue entries reclaimed");
	}
	std::cout << "Damage dispatch / visual ordering regression: " << failures << " failures\n";
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
