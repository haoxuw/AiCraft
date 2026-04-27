cmake_minimum_required(VERSION 3.20)
project(AiCraft VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SOLARIUM_PERF "Enable server perf instrumentation (frame/tick/handler timing)" OFF)
if(SOLARIUM_PERF)
	add_compile_definitions(SOLARIUM_PERF)
	message(STATUS "SOLARIUM_PERF: enabled (perf instrumentation compiled in)")
endif()

option(SOLARIUM_PATHFINDING_DEBUG "Enable pathfinding lifecycle logging via PATHLOG(...)" OFF)
if(SOLARIUM_PATHFINDING_DEBUG)
	add_compile_definitions(SOLARIUM_PATHFINDING_DEBUG)
	message(STATUS "SOLARIUM_PATHFINDING_DEBUG: enabled (PATHLOG expands to entityLog)")
endif()

# Solarium is a native C++ voxel sandbox. All code lives under src/:
#
#   src/platform/logic/    Pure data types (entity, chunk, inventory, …) — no deps
#   src/platform/net/      TCP protocol
#   src/platform/server/   Authoritative simulation + Solarium world gen + content
#   src/platform/agent/    Agent client runtime (Python-embedded AI)
#   src/platform/client/   Vulkan client (game loop, rendering, input)
#   src/python/            Python behaviors + helpers (copied into $BUILD_DIR)
#   src/artifacts/         Modder-facing game content (models, living, items, …)
#   src/config/            Runtime config (loaded from $BUILD_DIR/config)
#   src/resources/         Textures, sounds
#
# Each executable runs with CWD = $BUILD_DIR, so assets copied into
# $BUILD_DIR by POST_BUILD are found via "shaders/...", "artifacts/...", etc.

# --- Dependencies via FetchContent ---
include(FetchContent)

FetchContent_Declare(glm URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(zstd
	GIT_REPOSITORY https://github.com/facebook/zstd.git
	GIT_TAG v1.5.6
	SOURCE_SUBDIR build/cmake
)
set(ZSTD_BUILD_PROGRAMS    OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zstd)

set(Python3_ROOT_DIR "/usr")
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
FetchContent_Declare(pybind11
	GIT_REPOSITORY https://github.com/pybind/pybind11.git
	GIT_TAG v2.13.6
)
FetchContent_MakeAvailable(pybind11)

find_package(Vulkan REQUIRED)

# --- Assimp (GLB importer for voxel_earth pipeline) ---
# System libassimp-dev (>= 5.3) ships with KHR_draco_mesh_compression support
# enabled, which is what Photorealistic 3D Tiles use.
find_package(assimp REQUIRED)

# --- Chromium Embedded Framework (CEF) ---
# Vendored binary distribution at third_party/cef/ (gitignored, ~1.4 GB).
# Populate via `make cef_setup`. Linux x64 only today; macOS/Windows would
# require a different distribution tarball + helper-app layout (see
# docs/CEF_UI_PLAN.md §2). The CEF cmake module gives us libcef_dll_wrapper
# and the CEF_BINARY_FILES / CEF_RESOURCE_FILES lists for POST_BUILD copies.
set(CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef")
if(NOT EXISTS "${CEF_ROOT}/Release/libcef.so")
	message(FATAL_ERROR
		"CEF binary distribution not found at ${CEF_ROOT}.\n"
		"Run `make cef_setup` to download it.")
endif()
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")
# The minimal dist ships only Release/. CEF's cmake module picks Debug/ when
# CMAKE_BUILD_TYPE=Debug, so we always reference CEF_BINARY_DIR_RELEASE
# directly when copying or linking.
find_package(CEF REQUIRED)
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

# --- Engine sources ---
# Header-only except for the .cpp files listed explicitly.
set(PLATFORM_SERVER_SOURCES
	src/platform/server/server.h
	src/platform/server/server.cpp
	src/platform/server/entity_manager.h
	src/platform/agent/behavior.h
	src/platform/server/behavior_store.h
	src/platform/server/chunk_gen_service.h
	src/platform/server/chunk_gen_service.cpp
	src/platform/server/client_manager.h
	src/platform/python/python_bridge.h
	src/platform/python/python_bridge.cpp
	src/platform/server/server_tuning.h
	src/platform/server/test_server.h
	src/platform/server/builtin.h
	src/platform/server/builtin.cpp
	src/platform/agent/pathfind.h
	src/platform/agent/pathfind.cpp
	src/platform/agent/actions.h
	src/platform/agent/actions.cpp
	src/platform/agent/jitter.h
	src/platform/client/path_executor.h
	src/platform/client/path_executor.cpp
	# voxel_earth: server runtime needs region.cpp for VEAR file loading.
	# The decode/voxelize/texture stages stay confined to the bake CLI.
	src/platform/server/voxel_earth/region.h
	src/platform/server/voxel_earth/region.cpp
	src/platform/server/voxel_earth/palette.h
)

set(PLATFORM_CLIENT_SOURCES
	src/platform/client/audio.h
	src/platform/client/audio.cpp
	src/platform/client/audio_capture.h
	src/platform/client/audio_capture.cpp
	src/platform/client/camera.h
	src/platform/client/camera.cpp
	src/platform/client/settings.h
	src/platform/client/settings.cpp
	src/platform/client/plaza_animator.h
	src/platform/client/plaza_animator.cpp
	src/platform/client/chunk_mesher.h
	src/platform/client/chunk_mesher.cpp
	src/platform/logic/block_shape.h
	src/platform/logic/block_shape.cpp
)

# LLM sidecar client (HTTP/SSE → llama-server). Client-side only — server
# never touches these (Rule 5). See src/platform/llm/llm_client.h.
set(PLATFORM_LLM_SOURCES
	src/platform/llm/llm_client.h
	src/platform/llm/llm_client.cpp
	src/platform/llm/llm_session.h
	src/platform/llm/llm_sidecar.h
	src/platform/llm/whisper_sidecar.h
	src/platform/llm/whisper_client.h
	src/platform/llm/whisper_client.cpp
	src/platform/llm/tts_sidecar.h
	src/platform/llm/tts_client.h
)

set(GAME_INCLUDE_DIRS src/platform)

# === SPIR-V shader compilation ===
find_program(GLSLANG glslangValidator REQUIRED)
set(VK_SHADER_SRC_DIR ${CMAKE_SOURCE_DIR}/src/platform/shaders/vk)
set(VK_SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders/vk)
file(MAKE_DIRECTORY ${VK_SHADER_OUT_DIR})
set(VK_SHADER_SOURCES
	${VK_SHADER_SRC_DIR}/cube.vert
	${VK_SHADER_SRC_DIR}/cube.frag
	${VK_SHADER_SRC_DIR}/voxel.vert
	${VK_SHADER_SRC_DIR}/voxel.frag
	${VK_SHADER_SRC_DIR}/sky.vert
	${VK_SHADER_SRC_DIR}/sky.frag
	${VK_SHADER_SRC_DIR}/composite.vert
	${VK_SHADER_SRC_DIR}/composite.frag
	${VK_SHADER_SRC_DIR}/shadow.vert
	${VK_SHADER_SRC_DIR}/shadow.frag
	${VK_SHADER_SRC_DIR}/boxmodel.vert
	${VK_SHADER_SRC_DIR}/boxshadow.vert
	${VK_SHADER_SRC_DIR}/particle.vert
	${VK_SHADER_SRC_DIR}/particle.frag
	${VK_SHADER_SRC_DIR}/ribbon.vert
	${VK_SHADER_SRC_DIR}/ribbon.frag
	${VK_SHADER_SRC_DIR}/crack_overlay.vert
	${VK_SHADER_SRC_DIR}/crack_overlay.frag
	${VK_SHADER_SRC_DIR}/ghost_box.vert
	${VK_SHADER_SRC_DIR}/ghost_box.frag
	${VK_SHADER_SRC_DIR}/text2d.vert
	${VK_SHADER_SRC_DIR}/text2d.frag
	${VK_SHADER_SRC_DIR}/cef_overlay.vert
	${VK_SHADER_SRC_DIR}/cef_overlay.frag
	${VK_SHADER_SRC_DIR}/chunk_terrain.vert
	${VK_SHADER_SRC_DIR}/chunk_terrain.frag
	${VK_SHADER_SRC_DIR}/shadow_chunk.vert
)
set(VK_SPV_OUTPUTS "")
foreach(SHADER ${VK_SHADER_SOURCES})
	get_filename_component(FNAME ${SHADER} NAME)
	set(SPV ${VK_SHADER_OUT_DIR}/${FNAME}.spv)
	add_custom_command(
		OUTPUT ${SPV}
		COMMAND ${GLSLANG} -V ${SHADER} -o ${SPV}
		DEPENDS ${SHADER}
		COMMENT "SPIR-V: ${FNAME}"
		VERBATIM
	)
	list(APPEND VK_SPV_OUTPUTS ${SPV})
endforeach()
add_custom_target(vk_shaders DEPENDS ${VK_SPV_OUTPUTS})

# Reusable asset-staging helper. Called after every exe that needs python/
# artifacts/ at CWD (either for embedded Python or for the child
# solarium-server process it spawns).
function(stage_runtime_assets target)
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/python $<TARGET_FILE_DIR:${target}>/python
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/artifacts $<TARGET_FILE_DIR:${target}>/artifacts
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/config $<TARGET_FILE_DIR:${target}>/config
	)
endfunction()

# === GUI client (Vulkan) ===
# Singleplayer spawns solarium-server as a child and connects via localhost TCP.
# --host/--port pre-fills the join form for network play.
add_executable(solarium-ui-vk
	src/platform/client/action_icon.h
	src/platform/client/action_icon.cpp
	src/platform/client/main.cpp
	src/platform/client/game_vk.h
	src/platform/client/game_vk.cpp
	src/platform/client/game_vk_playing.cpp
	src/platform/client/game_vk_render.cpp
	src/platform/client/game_vk_renderers.h
	src/platform/client/game_vk_renderer_world.cpp
	src/platform/client/game_vk_renderer_hud.cpp
	src/platform/client/game_vk_renderer_menu.cpp
	src/platform/client/game_vk_renderer_panel.cpp
	src/platform/client/game_vk_renderer_entity_ui.cpp
	src/platform/client/game_vk_renderer_inventory.cpp
	src/platform/client/weather_effects.h
	src/platform/client/weather_effects.cpp
	src/platform/client/cef_browser_host.h
	src/platform/client/cef_browser_host.cpp
	src/platform/client/cef_app.h
	src/platform/client/cef_app.cpp
	src/platform/client/loading_screen.h
	src/platform/client/loading_screen.cpp
	src/platform/client/menu_plaza.h
	src/platform/client/menu_plaza.cpp
	src/platform/client/ui_kit.h
	src/platform/client/ui_kit.cpp
	src/platform/client/rhi/rhi.h
	src/platform/client/rhi/rhi_vk.h
	src/platform/client/rhi/rhi_vk.cpp
	src/platform/client/rhi/rhi_ui.cpp
	${PLATFORM_CLIENT_SOURCES}
	${PLATFORM_SERVER_SOURCES}
	${PLATFORM_LLM_SOURCES}
)
target_include_directories(solarium-ui-vk PRIVATE ${GAME_INCLUDE_DIRS} ${CEF_ROOT})
target_compile_definitions(solarium-ui-vk PRIVATE AICRAFT_RHI_VK)
target_link_libraries(solarium-ui-vk PRIVATE
	glfw glm::glm Vulkan::Vulkan
	Python3::Python pybind11::embed
	libzstd_static
	libcef_dll_wrapper
	"${CEF_BINARY_DIR_RELEASE}/libcef.so"
)
add_dependencies(solarium-ui-vk vk_shaders)

add_custom_command(TARGET solarium-ui-vk POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${VK_SHADER_OUT_DIR} $<TARGET_FILE_DIR:solarium-ui-vk>/shaders/vk
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/src/platform/fonts $<TARGET_FILE_DIR:solarium-ui-vk>/fonts
	COMMAND cp -ru ${CMAKE_SOURCE_DIR}/src/resources $<TARGET_FILE_DIR:solarium-ui-vk>/
)
stage_runtime_assets(solarium-ui-vk)

# CEF runtime files must sit alongside solarium-ui-vk: libcef.so itself plus
# Chromium's GPU-process fallback ICD (libvk_swiftshader.so + libvulkan.so.1
# + vk_swiftshader_icd.json). Without the SwiftShader bundle, OnPaint fires
# but every buffer is empty — Chromium's GPU subprocess can't initialize a
# rasterizer. Side-effect: this directory now ships its own libvulkan.so.1
# that would shadow our system NVIDIA driver unless the RUNPATH below is set
# correctly.
COPY_FILES(solarium-ui-vk "${CEF_BINARY_FILES}"
	"${CEF_BINARY_DIR_RELEASE}" "$<TARGET_FILE_DIR:solarium-ui-vk>")
COPY_FILES(solarium-ui-vk "${CEF_RESOURCE_FILES}"
	"${CEF_RESOURCE_DIR}" "$<TARGET_FILE_DIR:solarium-ui-vk>")

# RUNPATH order matters: ld searches it left-to-right before ld.so.cache.
# We list /usr/lib/x86_64-linux-gnu first so libvulkan.so.1 resolves to the
# system NVIDIA driver (what solarium-ui-vk's renderer needs), not the
# SwiftShader copy that ships next to libcef.so. $ORIGIN stays in the path
# so libcef.so + its non-Vulkan peers still load.
set_target_properties(solarium-ui-vk PROPERTIES
	BUILD_RPATH   "/usr/lib/x86_64-linux-gnu:\$ORIGIN"
	INSTALL_RPATH "/usr/lib/x86_64-linux-gnu:\$ORIGIN"
	BUILD_WITH_INSTALL_RPATH TRUE
)

# === CEF subprocess shim ===
# Chromium re-execs the binary path passed in CefSettings.browser_subprocess_path
# for every renderer/GPU/utility child. If we used solarium-ui-vk for that,
# each child would re-init Python/GLFW/audio/LLM. Instead we ship a tiny binary
# whose only job is to call CefExecuteProcess and return.
add_executable(solarium-cef-subprocess
	src/platform/client/cef_subprocess_main.cpp
	src/platform/client/cef_app.h
	src/platform/client/cef_app.cpp
)
target_include_directories(solarium-cef-subprocess PRIVATE ${CEF_ROOT} ${GAME_INCLUDE_DIRS})
target_link_libraries(solarium-cef-subprocess PRIVATE
	libcef_dll_wrapper
	"${CEF_BINARY_DIR_RELEASE}/libcef.so"
)
# Subprocess sits in the same directory as the main binary so a single
# rpath of $ORIGIN finds libcef.so for both. Force the same TARGET_FILE_DIR
# and pin rpath to keep the SwiftShader libs (if anyone re-adds them) from
# hijacking our Vulkan, same as solarium-ui-vk above.
set_target_properties(solarium-cef-subprocess PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:solarium-ui-vk>"
	BUILD_RPATH   "\$ORIGIN"
	INSTALL_RPATH "\$ORIGIN"
	BUILD_WITH_INSTALL_RPATH TRUE
)
add_dependencies(solarium-ui-vk solarium-cef-subprocess)

# === Dedicated server (headless) ===
add_executable(solarium-server
	src/platform/server/main.cpp
	${PLATFORM_SERVER_SOURCES}
)
target_include_directories(solarium-server PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries(solarium-server PRIVATE
	glm::glm
	Python3::Python pybind11::embed
	libzstd_static
)
stage_runtime_assets(solarium-server)

# === Headless E2E test runner ===
add_executable(solarium-test
	src/tests/test_e2e.cpp
	${PLATFORM_SERVER_SOURCES}
)
target_include_directories(solarium-test PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries(solarium-test PRIVATE
	glm::glm
	Python3::Python pybind11::embed
	libzstd_static
)
stage_runtime_assets(solarium-test)

# === Pathfinding regression tests ===
add_executable(solarium-test-pathfinding
	src/tests/test_pathfinding.cpp
	${PLATFORM_SERVER_SOURCES}
)
target_include_directories(solarium-test-pathfinding PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries(solarium-test-pathfinding PRIVATE
	glm::glm
	Python3::Python pybind11::embed
	libzstd_static
)
stage_runtime_assets(solarium-test-pathfinding)

# === Separation regression tests ===
# Pure-function tests for src/platform/agent/separation.h. No TestServer —
# scenarios drive applySeparation with curated inputs and assert outputs.
add_executable(solarium-test-separation
	src/tests/test_separation.cpp
)
target_include_directories(solarium-test-separation PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries(solarium-test-separation PRIVATE
	glm::glm
)

# === Whisper smoke test (phase-2 STT) ===
# Tiny one-shot: WAV → whisper-server → stdout. Not shipped; only for
# verifying WhisperClient outside the GUI client.
add_executable(solarium-whisper-smoke
	src/platform/llm/whisper_smoke.cpp
	src/platform/llm/whisper_client.h
	src/platform/llm/whisper_client.cpp
)
target_include_directories(solarium-whisper-smoke PRIVATE ${GAME_INCLUDE_DIRS})

# === Piper/TTS smoke test (phase-3) ===
# Spawns piper, synthesises one sentence, prints the output WAV path.
add_executable(solarium-tts-smoke
	src/platform/llm/tts_smoke.cpp
	src/platform/llm/tts_client.h
	src/platform/llm/tts_sidecar.h
)
target_include_directories(solarium-tts-smoke PRIVATE ${GAME_INCLUDE_DIRS})

# === Voice mux resolution test (phase-4) ===
# Asserts dialog_voice → .onnx routing; does not spawn any piper child.
add_executable(solarium-mux-smoke
	src/platform/llm/mux_smoke.cpp
	src/platform/llm/tts_voice_mux.h
	src/platform/llm/tts_client.h
	src/platform/llm/tts_sidecar.h
)
target_include_directories(solarium-mux-smoke PRIVATE ${GAME_INCLUDE_DIRS})

# === Voxel Earth GLB loader smoke test (phase-2) ===
# Loads one cached Photorealistic 3D Tile and prints mesh stats. Verifies
# Assimp + Draco decode end-to-end before we wire voxelization in.
set(VOXEL_EARTH_SOURCES
	src/platform/server/voxel_earth/glb_loader.h
	src/platform/server/voxel_earth/glb_loader.cpp
	src/platform/server/voxel_earth/rotate.h
	src/platform/server/voxel_earth/rotate.cpp
	src/platform/server/voxel_earth/texture.h
	src/platform/server/voxel_earth/texture.cpp
	src/platform/server/voxel_earth/voxelizer.h
	src/platform/server/voxel_earth/voxelizer.cpp
	src/platform/server/voxel_earth/region.h
	src/platform/server/voxel_earth/region.cpp
)

add_executable(solarium-voxel-smoke
	src/platform/server/voxel_earth/voxel_smoke.cpp
	${VOXEL_EARTH_SOURCES}
)
target_include_directories(solarium-voxel-smoke PRIVATE
	${GAME_INCLUDE_DIRS}
	${CMAKE_SOURCE_DIR}/third_party/stb
)
target_link_libraries(solarium-voxel-smoke PRIVATE assimp::assimp)

# === Voxel Earth bake CLI (phase-4) ===
# Reads ~/.voxel/google/glb/*.glb, voxelizes them under a shared ECEF origin,
# writes a single VEAR-format region file ready for in-server lookup.
add_executable(solarium-voxel-bake
	src/platform/server/voxel_earth/voxel_bake.cpp
	${VOXEL_EARTH_SOURCES}
)
target_include_directories(solarium-voxel-bake PRIVATE
	${GAME_INCLUDE_DIRS}
	${CMAKE_SOURCE_DIR}/third_party/stb
)
target_link_libraries(solarium-voxel-bake PRIVATE assimp::assimp)
target_include_directories(solarium-voxel-smoke PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries(solarium-voxel-smoke PRIVATE assimp::assimp)
