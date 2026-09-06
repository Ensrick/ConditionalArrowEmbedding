#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace ConditionalArrowEmbedding::Version {
inline constexpr std::string_view Name = "Conditional Arrow Embedding";
inline constexpr std::string_view ShortName = "ConditionalArrowEmbedding";
inline constexpr std::string_view Semantic = "0.3.2";
inline constexpr std::array<std::uint16_t, 4> SupportedRuntime{1, 7, 104, 0};
} // namespace ConditionalArrowEmbedding::Version
