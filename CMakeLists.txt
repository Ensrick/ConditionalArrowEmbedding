cmake_minimum_required(VERSION 3.24)

project(ConditionalArrowEmbedding VERSION 0.2.0 LANGUAGES CXX)

if(MSVC)
    add_compile_options("/experimental:deterministic" "/pathmap:${CMAKE_SOURCE_DIR}=.")
    add_link_options("/Brepro")
endif()

set(BUILD_TESTS OFF CACHE BOOL "Do not build CommonLib tests" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Build Skyrim SE support" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Build Skyrim AE support" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "VR is not supported" FORCE)
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "Enable trampoline support" FORCE)

add_subdirectory(extern/CommonLibSSE-NG)
include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)
target_compile_definitions(CommonLibSSE PUBLIC COMMONLIBSSE_NO_MODAL_ERRORS=1)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(ConditionalArrowEmbeddingCore STATIC
    src/Config.cpp
    src/Config.h
    src/Policy.cpp
    src/Policy.h
)
target_compile_features(ConditionalArrowEmbeddingCore PUBLIC cxx_std_23)
target_include_directories(ConditionalArrowEmbeddingCore PUBLIC src)
target_link_libraries(ConditionalArrowEmbeddingCore PUBLIC nlohmann_json::nlohmann_json)

add_commonlibsse_plugin(${PROJECT_NAME}
    AUTHOR "Ensrick"
    MINIMUM_SKSE_VERSION "2.3.1"
    COMPATIBLE_RUNTIMES "1.7.104.0"
    SOURCES
        src/Hooks.cpp
        src/Hooks.h
        src/PCH.h
        src/Plugin.cpp
        src/Version.h
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_link_libraries(${PROJECT_NAME} PRIVATE ConditionalArrowEmbeddingCore)

add_executable(ConditionalArrowEmbeddingTests tests/PolicyTests.cpp)
target_compile_features(ConditionalArrowEmbeddingTests PRIVATE cxx_std_23)
target_compile_options(ConditionalArrowEmbeddingTests PRIVATE /W4 /WX)
target_link_libraries(ConditionalArrowEmbeddingTests PRIVATE ConditionalArrowEmbeddingCore)

enable_testing()
add_test(NAME PolicyTests COMMAND ConditionalArrowEmbeddingTests)

if(NOT DEFINED OUTPUT_FOLDER)
    set(OUTPUT_FOLDER "${CMAKE_BINARY_DIR}/stage")
endif()
set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")

add_custom_target(ConditionalArrowEmbeddingStage ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/config/ConditionalArrowEmbedding.json" "${DLL_FOLDER}/ConditionalArrowEmbedding.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/config/ConditionalArrowEmbedding.schema.json" "${DLL_FOLDER}/ConditionalArrowEmbedding.schema.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${OUTPUT_FOLDER}/LICENSE"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md" "${OUTPUT_FOLDER}/CHANGELOG.md"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${OUTPUT_FOLDER}/README.md"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/THIRD-PARTY-NOTICES.md" "${OUTPUT_FOLDER}/THIRD-PARTY-NOTICES.md"
    DEPENDS ${PROJECT_NAME}
    VERBATIM)
