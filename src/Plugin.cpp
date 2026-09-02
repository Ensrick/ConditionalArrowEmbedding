#include "PCH.h"

#include "Config.h"
#include "Hooks.h"
#include "Version.h"

namespace {
void SetupLogging() {
	auto directory = SKSE::log::log_directory();
	if (!directory) {
		return;
	}
	*directory /= "ConditionalArrowEmbedding.log";
	try {
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(directory->string(), true);
		auto log = std::make_shared<spdlog::logger>("ConditionalArrowEmbedding", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
	} catch (...) {
		// Logging must never show a modal dialog or prevent Skyrim startup.
	}
}

[[nodiscard]] std::filesystem::path ConfigPath() {
	std::array<wchar_t, 32768> executablePath{};
	const auto length = REX::W32::GetModuleFileNameW(nullptr, executablePath.data(),
	                                                 static_cast<std::uint32_t>(executablePath.size()));
	if (length == 0 || length >= executablePath.size()) {
		throw std::runtime_error("could not resolve Skyrim executable path");
	}
	return std::filesystem::path(std::wstring_view(executablePath.data(), length)).parent_path() / "Data" /
	       "SKSE" / "Plugins" / "ConditionalArrowEmbedding.json";
}
} // namespace

SKSEPluginLoad(const SKSE::LoadInterface *a_skse) {
	SetupLogging();
	try {
		const auto skseVersion = REL::Version::unpack(a_skse->SKSEVersion());
		logger::info("{} {} loading on runtime {} with SKSE {}", ConditionalArrowEmbedding::Version::Name,
		             ConditionalArrowEmbedding::Version::Semantic, a_skse->RuntimeVersion().string(),
		             skseVersion.string());
		if (a_skse->RuntimeVersion() != REL::Version(1, 7, 104, 0)) {
			logger::critical("unsupported Skyrim runtime {}; verified runtime is 1.7.104.0",
			                 a_skse->RuntimeVersion().string());
			return false;
		}
		if (skseVersion != REL::Version(2, 3, 1, 0)) {
			logger::critical("unsupported SKSE {}; verified SKSE is 2.3.1.0", skseVersion.string());
			return false;
		}

		// Preserve the project-owned file logger. CommonLib's default logger
		// initialization would replace it and truncate pre-initialization evidence.
		SKSE::Init(a_skse, false);
		auto config = ConditionalArrowEmbedding::LoadConfig(ConfigPath());
		spdlog::set_level(config.debugLogging ? spdlog::level::debug : spdlog::level::info);
		if (!ConditionalArrowEmbedding::Hooks::Install(std::move(config))) {
			return false;
		}
		logger::info("plugin loaded successfully");
		return true;
	} catch (const std::exception &error) {
		logger::critical("plugin load failed safely: {}", error.what());
		return false;
	} catch (...) {
		logger::critical("plugin load failed safely with an unknown exception");
		return false;
	}
}
