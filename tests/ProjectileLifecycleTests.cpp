#include "PCH.h"
#include "ProjectileLifecycle.h"

#include <cstring>
#include <iostream>

int main() {
	if (!REL::Module::mock(REL::Version(1, 7, 104, 0), REL::Module::Runtime::AE, L"SkyrimSE.exe", 0x1000)) {
		return 1;
	}
	alignas(RE::Projectile) std::array<std::byte, 0x220> memory{};
	const auto *projectile = reinterpret_cast<const RE::Projectile *>(memory.data());
	const auto &data = projectile->GetProjectileRuntimeData();
	if (reinterpret_cast<const std::byte *>(&data.flags) - memory.data() != 0x1D4 ||
	    reinterpret_cast<const std::byte *>(&data.explosion) - memory.data() != 0x158) {
		std::cerr << "Projectile lifecycle accessors do not match the reviewed 1.7.104 engine layout\n";
		return 1;
	}
	for (const bool destroyAfterHit : {false, true}) {
		for (const bool hitscan : {false, true}) {
			for (const bool chainShatter : {false, true}) {
				for (const bool explosion : {false, true}) {
					const std::uint32_t flags =
					    (destroyAfterHit ? static_cast<std::uint32_t>(RE::Projectile::Flags::kDestroyAfterHit)
					                     : 0U) |
					    (hitscan ? static_cast<std::uint32_t>(RE::Projectile::Flags::kHitScan) : 0U) |
					    (chainShatter ? static_cast<std::uint32_t>(RE::Projectile::Flags::kChainShatter)
					                  : 0U);
					// Compared only: the production helper must never dereference the explosion pointer.
					const std::uintptr_t explosionAddress = explosion ? 1U : 0U;
					std::memcpy(memory.data() + 0x1D4, &flags, sizeof(flags));
					std::memcpy(memory.data() + 0x158, &explosionAddress, sizeof(explosionAddress));
					if (ConditionalArrowEmbedding::HasUnsupportedDeferredLifecycle(*projectile) !=
					    (explosion || chainShatter)) {
						std::cerr << "Ordinary arrow was excluded, or a special lifecycle was admitted\n";
						return 1;
					}
				}
			}
		}
	}
	REL::Module::reset();
	std::cout << "16 projectile lifecycle layout/flag regression cases passed\n";
	return 0;
}
