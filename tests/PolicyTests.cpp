#include "Policy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {
using ConditionalArrowEmbedding::ClassifyHitRegion;
using ConditionalArrowEmbedding::DecideImpact;
using ConditionalArrowEmbedding::HitRegion;
using ConditionalArrowEmbedding::ImpactAction;
using ConditionalArrowEmbedding::ImpactContext;
using ConditionalArrowEmbedding::IsLivingHumanoidTarget;
using ConditionalArrowEmbedding::PostDamageState;
using ConditionalArrowEmbedding::TargetTypeTraits;
using ConditionalArrowEmbedding::WasKilledByHit;

int failures = 0;

void Expect(const bool a_condition, const char *a_name) {
	if (!a_condition) {
		std::cerr << "FAILED: " << a_name << '\n';
		++failures;
	}
}

ImpactContext BodyAt(const double a_healthRatio) {
	ImpactContext context{};
	context.region = HitRegion::Body;
	context.healthRatioAfter = a_healthRatio;
	return context;
}
} // namespace

int main() {
	{
		TargetTypeTraits traits{.hasRace = true, .actorTypeNPC = true};
		Expect(IsLivingHumanoidTarget(traits), "ActorTypeNPC actor with a race is eligible");
		traits.actorTypeNPC = false;
		Expect(!IsLivingHumanoidTarget(traits), "untagged race fails open");
		traits.actorTypeNPC = true;
		traits.hasRace = false;
		Expect(!IsLivingHumanoidTarget(traits), "missing race fails open");
	}
	{
		using Member = bool TargetTypeTraits::*;
		const std::vector<std::pair<Member, const char *>> exclusions{
		    {&TargetTypeTraits::actorTypeAnimal, "animal excluded"},
		    {&TargetTypeTraits::actorTypeCreature, "creature excluded"},
		    {&TargetTypeTraits::actorTypeDaedra, "daedra excluded"},
		    {&TargetTypeTraits::actorTypeDragon, "dragon excluded"},
		    {&TargetTypeTraits::actorTypeDwarven, "construct excluded"},
		    {&TargetTypeTraits::actorTypeGhost, "ghost excluded"},
		    {&TargetTypeTraits::actorTypeUndead, "undead excluded"},
		    {&TargetTypeTraits::ghost, "engine ghost state excluded"},
		    {&TargetTypeTraits::reanimated, "reanimated actor excluded"},
		    {&TargetTypeTraits::excludedVanillaUtilityRace, "vanilla utility race excluded"},
		};
		for (const auto &[member, name] : exclusions) {
			TargetTypeTraits traits{.hasRace = true, .actorTypeNPC = true};
			traits.*member = true;
			Expect(!IsLivingHumanoidTarget(traits), name);
		}
	}
	{
		Expect(WasKilledByHit(PostDamageState{
		           .targetWasAlive = false, .hitMarkedFatal = true, .healthAfter = 0.0}) == false,
		       "pre-existing death is not attributed to hit");
		Expect(WasKilledByHit(PostDamageState{.hitMarkedFatal = true}), "fatal HitData signal");
		Expect(WasKilledByHit(PostDamageState{.targetReportsDead = true}), "engine dead signal");
		Expect(WasKilledByHit(PostDamageState{.targetLifeStateDyingOrDead = true}),
		       "dying life-state signal");
		Expect(WasKilledByHit(PostDamageState{.healthAfter = 0.0}), "zero-health lethal fallback");
		Expect(WasKilledByHit(PostDamageState{.healthAfter = -10.0}), "negative-health lethal fallback");
		Expect(!WasKilledByHit(PostDamageState{.targetInNonlethalDownState = true, .healthAfter = 0.0}),
		       "essential bleedout is not lethal");
		Expect(!WasKilledByHit(PostDamageState{.hitMarkedFatal = true,
		                                       .targetReportsDead = true,
		                                       .targetInNonlethalDownState = true,
		                                       .healthAfter = 0.0}),
		       "explicit nonlethal down state overrides ambiguous fatal signals");
		Expect(!WasKilledByHit(PostDamageState{.healthAfter = 0.01}), "positive health is nonlethal");
		Expect(!WasKilledByHit(PostDamageState{.healthAfter = std::numeric_limits<double>::quiet_NaN()}),
		       "unknown health fails open");
	}
	{
		auto context = BodyAt(1.0);
		context.enabled = false;
		Expect(DecideImpact(context) == ImpactAction::PreserveVanilla, "disabled");
	}
	{
		auto context = BodyAt(1.0);
		context.vanillaWouldEmbed = false;
		Expect(DecideImpact(context) == ImpactAction::PreserveVanilla, "vanilla non-embed");
	}
	{
		auto context = BodyAt(1.0);
		context.blockedOrAlreadyRicocheting = true;
		Expect(DecideImpact(context) == ImpactAction::PreserveVanilla, "blocked hit");
	}
	{
		auto context = BodyAt(1.0);
		context.targetWasAlive = false;
		Expect(DecideImpact(context) == ImpactAction::PreserveVanilla, "already dead target");
	}
	{
		auto context = BodyAt(1.0);
		context.targetIsPlayer = true;
		context.affectPlayer = false;
		Expect(DecideImpact(context) == ImpactAction::PreserveVanilla, "player disabled");
	}
	{
		auto context = BodyAt(1.0);
		context.affectNPCs = false;
		Expect(DecideImpact(context) == ImpactAction::PreserveVanilla, "NPCs disabled");
	}
	{
		for (const auto region : {HitRegion::Unknown, HitRegion::Head, HitRegion::Body}) {
			for (const auto health : {0.1, 0.5, 1.0}) {
				for (const auto killed : {false, true}) {
					auto context = BodyAt(health);
					context.region = region;
					context.targetKilledByHit = killed;
					context.targetIsLivingHumanoid = false;
					Expect(DecideImpact(context) == ImpactAction::PreserveVanilla,
					       "non-humanoid target always preserves vanilla");
				}
			}
		}
	}
	{
		auto context = BodyAt(1.0);
		context.targetKilledByHit = true;
		Expect(DecideImpact(context) == ImpactAction::PreserveVanilla, "lethal body hit may stick");
		context.region = HitRegion::Head;
		Expect(DecideImpact(context) == ImpactAction::PreserveVanilla, "lethal head hit may stick");
	}
	{
		ImpactContext context{};
		context.region = HitRegion::Head;
		context.healthRatioAfter = 0.9;
		Expect(DecideImpact(context) == ImpactAction::Bounce, "nonlethal head hit at high health");
		context.healthRatioAfter = 0.1;
		Expect(DecideImpact(context) == ImpactAction::Bounce, "nonlethal head hit at low health");
	}
	{
		Expect(DecideImpact(BodyAt(1.0)) == ImpactAction::Bounce, "healthy body hit");
		Expect(DecideImpact(BodyAt(0.5)) == ImpactAction::Bounce, "body hit exactly at threshold");
		Expect(DecideImpact(BodyAt(0.4999)) == ImpactAction::PreserveVanilla,
		       "body hit below threshold may stick");
	}
	{
		auto context = BodyAt(std::numeric_limits<double>::quiet_NaN());
		Expect(DecideImpact(context) == ImpactAction::PreserveVanilla, "invalid health fails open");
		context.region = HitRegion::Unknown;
		context.healthRatioAfter = 1.0;
		Expect(DecideImpact(context) == ImpactAction::PreserveVanilla, "unknown region fails open");
	}

	const std::vector<std::string> tokens{"head", "skull", "face", "eye", "jaw", "brow", "nose"};
	const std::vector<std::string> empty{};
	const std::vector<std::string> headNode{"NPC Head [Head]"};
	const std::vector<std::string> eyeNode{"NPC R Eye [REye]"};
	const std::vector<std::string> spineNode{"NPC Spine2 [Spn2]"};
	Expect(ClassifyHitRegion(1, empty, tokens) == HitRegion::Head, "engine head limb");
	Expect(ClassifyHitRegion(2, empty, tokens) == HitRegion::Head, "engine eye limb");
	Expect(ClassifyHitRegion(0, empty, tokens) == HitRegion::Body, "engine torso limb");
	Expect(ClassifyHitRegion(-1, headNode, tokens) == HitRegion::Head, "head-node fallback");
	Expect(ClassifyHitRegion(-1, eyeNode, tokens) == HitRegion::Head, "eye-node fallback");
	Expect(ClassifyHitRegion(-1, spineNode, tokens) == HitRegion::Body, "non-head node is body");
	Expect(ClassifyHitRegion(-1, empty, tokens) == HitRegion::Unknown, "missing location data");

	if (failures != 0) {
		std::cerr << failures << " policy test(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "All Conditional Arrow Embedding policy tests passed\n";
	return EXIT_SUCCESS;
}
