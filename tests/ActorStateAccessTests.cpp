#include "ActorStateAccess.h"
#include "PCH.h"
#include "Policy.h"

#include <cstring>
#include <iostream>

int main() {
	// Model the actual 1.7.104 layout. No Skyrim process or game launch is used.
	if (!REL::Module::mock(REL::Version(1, 7, 104, 0), REL::Module::Runtime::AE, L"SkyrimSE.exe", 0x1000)) {
		return 1;
	}
	alignas(RE::Actor) std::array<std::byte, 0x400> memory{};
	const auto *actor = reinterpret_cast<const RE::Actor *>(memory.data());
	const auto *runtimeState = actor->AsActorState();
	const auto stateOffset = reinterpret_cast<const std::byte *>(runtimeState) - memory.data();
	if (stateOffset != 0xC0) {
		std::cerr << "1.7.104 ActorState accessor did not resolve to Actor+0xC0\n";
		return 1;
	}
	const auto legacyOffset =
	    reinterpret_cast<const std::byte *>(static_cast<const RE::ActorState *>(actor)) - memory.data() + 8;
	if (legacyOffset == 0xC8) {
		std::cerr << "Regression fixture must use the shipped multi-runtime layout\n";
		return 1;
	}
	for (std::uint32_t noiseState = 0; noiseState < 16; ++noiseState) {
		const auto noise = noiseState << 21U;
		std::memcpy(memory.data() + legacyOffset, &noise, sizeof(noise));
		for (std::uint32_t lifeState = 0; lifeState <= 8; ++lifeState) {
			const auto bits = lifeState << 21U;
			std::memcpy(memory.data() + 0xC8, &bits, sizeof(bits));
			const auto expected = lifeState == static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kReanimate);
			if (ConditionalArrowEmbedding::IsRuntimeReanimated(*actor) != expected) {
				std::cerr << "Runtime reanimation depended on unrelated actor bytes\n";
				return 1;
			}
			const ConditionalArrowEmbedding::TargetTypeTraits traits{
			    .hasRace = true,
			    .actorTypeNPC = true,
			    .reanimated = ConditionalArrowEmbedding::IsRuntimeReanimated(*actor)};
			ConditionalArrowEmbedding::ImpactContext context{};
			context.targetIsPlayer = true;
			context.targetIsLivingHumanoid = ConditionalArrowEmbedding::IsLivingHumanoidTarget(traits);
			context.region = ConditionalArrowEmbedding::HitRegion::Body;
			const auto expectedAction = expected ? ConditionalArrowEmbedding::ImpactAction::PreserveVanilla
			                                     : ConditionalArrowEmbedding::ImpactAction::Bounce;
			if (ConditionalArrowEmbedding::DecideImpact(context) != expectedAction) {
				std::cerr << "Full-health player decision depended on unrelated actor bytes\n";
				return 1;
			}
		}
	}
	REL::Module::reset();
	std::cout << "144 actor-layout regression cases passed; legacy state word offset=0x" << std::hex
	          << legacyOffset << ", runtime state word offset=0xC8\n";
	return 0;
}
