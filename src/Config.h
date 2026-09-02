#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ConditionalArrowEmbedding {
struct Config {
	std::uint32_t schemaVersion{1};
	bool enabled{true};
	bool affectPlayer{true};
	bool affectNPCs{true};
	bool debugLogging{false};
	double bodyStickBelowHealthRatio{0.5};
	std::uint32_t fallbackHeadAncestorDepth{3};
	std::vector<std::string> fallbackHeadNodeTokens{"head", "skull", "face", "eye", "jaw", "brow", "nose"};
};

[[nodiscard]] Config DefaultConfig();
[[nodiscard]] Config LoadConfig(const std::filesystem::path &a_path);
void ValidateConfig(const Config &a_config);
} // namespace ConditionalArrowEmbedding
