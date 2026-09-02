#include "Config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace ConditionalArrowEmbedding {
namespace {
[[nodiscard]] std::string NormalizeToken(std::string_view a_value) {
	std::string result;
	result.reserve(a_value.size());
	for (const auto character : a_value) {
		const auto byte = static_cast<unsigned char>(character);
		if (!std::isspace(byte)) {
			result.push_back(static_cast<char>(std::tolower(byte)));
		}
	}
	return result;
}
} // namespace

Config DefaultConfig() { return {}; }

void ValidateConfig(const Config &a_config) {
	if (a_config.schemaVersion != 1) {
		throw std::invalid_argument("schemaVersion must be 1");
	}
	if (!std::isfinite(a_config.bodyStickBelowHealthRatio) || a_config.bodyStickBelowHealthRatio < 0.0 ||
	    a_config.bodyStickBelowHealthRatio > 1.0) {
		throw std::invalid_argument("bodyStickBelowHealthRatio must be between 0 and 1");
	}
	if (a_config.fallbackHeadAncestorDepth > 8) {
		throw std::invalid_argument("fallbackHeadAncestorDepth must be at most 8");
	}
	if (a_config.fallbackHeadNodeTokens.empty()) {
		throw std::invalid_argument("fallbackHeadNodeTokens must not be empty");
	}
	for (const auto &token : a_config.fallbackHeadNodeTokens) {
		if (NormalizeToken(token).empty()) {
			throw std::invalid_argument("fallbackHeadNodeTokens must not contain empty values");
		}
	}
}

Config LoadConfig(const std::filesystem::path &a_path) {
	auto config = DefaultConfig();
	std::ifstream input(a_path);
	if (!input) {
		return config;
	}

	const auto document = nlohmann::json::parse(input);
	config.schemaVersion = document.value("schemaVersion", config.schemaVersion);
	config.enabled = document.value("enabled", config.enabled);
	config.affectPlayer = document.value("affectPlayer", config.affectPlayer);
	config.affectNPCs = document.value("affectNPCs", config.affectNPCs);
	config.debugLogging = document.value("debugLogging", config.debugLogging);
	config.bodyStickBelowHealthRatio =
	    document.value("bodyStickBelowHealthRatio", config.bodyStickBelowHealthRatio);
	config.fallbackHeadAncestorDepth =
	    document.value("fallbackHeadAncestorDepth", config.fallbackHeadAncestorDepth);
	config.fallbackHeadNodeTokens = document.value("fallbackHeadNodeTokens", config.fallbackHeadNodeTokens);

	for (auto &token : config.fallbackHeadNodeTokens) {
		token = NormalizeToken(token);
	}
	std::ranges::sort(config.fallbackHeadNodeTokens);
	config.fallbackHeadNodeTokens.erase(
	    std::unique(config.fallbackHeadNodeTokens.begin(), config.fallbackHeadNodeTokens.end()),
	    config.fallbackHeadNodeTokens.end());

	ValidateConfig(config);
	return config;
}
} // namespace ConditionalArrowEmbedding
