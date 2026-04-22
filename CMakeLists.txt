cmake_minimum_required(VERSION 3.20)
project(AiCraft VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CIVCRAFT_PERF "Enable server perf instrumentation (frame/tick/handler timing)" OFF)
if(CIVCRAFT_PERF)
	add_compile_definitions(CIVCRAFT_PERF)
	message(STATUS "CIVCRAFT_PERF: enabled (perf instrumentation compiled in)")
endif()

# CivCraft is a native C++ voxel sandbox. All code lives under src/:
#
#   src/platform/logic/    Pure data types (entity, chunk, inventory, …) — no deps
#   src/platform/net/      TCP protocol
#   src/platform/server/   Authoritative simulation + CivCraft world gen + content
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

# --- Engine sources ---
# Header-only except for the .cpp files listed explicitly.
set(PLATFORM_SERVER_SOURCES
	src/platform/server/server.h
	src/platform/server/server.cpp
	src/platform/server/entity_manager.h
	src/platform/server/behavior.h
	src/platform/server/behavior_store.h
	src/platform/server/chunk_gen_service.h
	src/platform/server/chunk_gen_service.cpp
	src/platform/server/client_manager.h
	src/platform/server/pathfind.h
	src/platform/server/python_bridge.h
	src/platform/server/python_bridge.cpp
	src/platform/server/server_tuning.h
	src/platform/server/test_server.h
	src/platform/server/builtin.h
	src/platform/server/builtin.cpp
	src/platform/agent/pathfind.h
	src/platform/agent/pathfind.cpp
)

set(PLATFORM_CLIENT_SOURCES
	src/platform/client/audio.h
	src/platform/client/audio.cpp
	src/platform/client/audio_capture.h
	src/platform/client/audio_capture.cpp
	src/platform/client/camera.h
	src/platform/client/camera.cpp
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
# civcraft-server process it spawns).
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
# Singleplayer spawns civcraft-server as a child and connects via localhost TCP.
# --host/--port pre-fills the join form for network play.
add_executable(civcraft-ui-vk
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
	src/platform/client/handbook_panel.h
	src/platform/client/handbook_panel.cpp
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
target_include_directories(civcraft-ui-vk PRIVATE ${GAME_INCLUDE_DIRS})
target_compile_definitions(civcraft-ui-vk PRIVATE AICRAFT_RHI_VK)
target_link_libraries(civcraft-ui-vk PRIVATE
	glfw glm::glm Vulkan::Vulkan
	Python3::Python pybind11::embed
	libzstd_static
)
add_dependencies(civcraft-ui-vk vk_shaders)

add_custom_command(TARGET civcraft-ui-vk POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${VK_SHADER_OUT_DIR} $<TARGET_FILE_DIR:civcraft-ui-vk>/shaders/vk
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/src/platform/fonts $<TARGET_FILE_DIR:civcraft-ui-vk>/fonts
	COMMAND cp -ru ${CMAKE_SOURCE_DIR}/src/resources $<TARGET_FILE_DIR:civcraft-ui-vk>/
)
stage_runtime_assets(civcraft-ui-vk)

# === Dedicated server (headless) ===
add_executable(civcraft-server
	src/platform/server/main.cpp
	${PLATFORM_SERVER_SOURCES}
)
target_include_directories(civcraft-server PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries(civcraft-server PRIVATE
	glm::glm
	Python3::Python pybind11::embed
	libzstd_static
)
stage_runtime_assets(civcraft-server)

# === Headless E2E test runner ===
add_executable(civcraft-test
	src/tests/test_e2e.cpp
	${PLATFORM_SERVER_SOURCES}
)
target_include_directories(civcraft-test PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries(civcraft-test PRIVATE
	glm::glm
	Python3::Python pybind11::embed
	libzstd_static
)
stage_runtime_assets(civcraft-test)

# === Pathfinding regression tests ===
add_executable(civcraft-test-pathfinding
	src/tests/test_pathfinding.cpp
	${PLATFORM_SERVER_SOURCES}
)
target_include_directories(civcraft-test-pathfinding PRIVATE ${GAME_INCLUDE_DIRS})
target_link_libraries(civcraft-test-pathfinding PRIVATE
	glm::glm
	Python3::Python pybind11::embed
	libzstd_static
)
stage_runtime_assets(civcraft-test-pathfinding)

# === Whisper smoke test (phase-2 STT) ===
# Tiny one-shot: WAV → whisper-server → stdout. Not shipped; only for
# verifying WhisperClient outside the GUI client.
add_executable(civcraft-whisper-smoke
	src/platform/llm/whisper_smoke.cpp
	src/platform/llm/whisper_client.h
	src/platform/llm/whisper_client.cpp
)
target_include_directories(civcraft-whisper-smoke PRIVATE ${GAME_INCLUDE_DIRS})

# === Piper/TTS smoke test (phase-3) ===
# Spawns piper, synthesises one sentence, prints the output WAV path.
add_executable(civcraft-tts-smoke
	src/platform/llm/tts_smoke.cpp
	src/platform/llm/tts_client.h
	src/platform/llm/tts_sidecar.h
)
target_include_directories(civcraft-tts-smoke PRIVATE ${GAME_INCLUDE_DIRS})

# === Voice mux resolution test (phase-4) ===
# Asserts dialog_voice → .onnx routing; does not spawn any piper child.
add_executable(civcraft-mux-smoke
	src/platform/llm/mux_smoke.cpp
	src/platform/llm/tts_voice_mux.h
	src/platform/llm/tts_client.h
	src/platform/llm/tts_sidecar.h
)
target_include_directories(civcraft-mux-smoke PRIVATE ${GAME_INCLUDE_DIRS})
