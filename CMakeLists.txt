cmake_minimum_required(VERSION 3.20)
project(AiCraft VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Perf instrumentation (frame/tick/handler timing in the dedicated server).
# Enabled only by `make game` via a separate build dir; production builds
# (`make server`, `make client`) leave this off so the code is not compiled in.
option(MODCRAFT_PERF "Enable server perf instrumentation (frame/tick/handler timing)" OFF)
if(MODCRAFT_PERF)
	add_compile_definitions(MODCRAFT_PERF)
	message(STATUS "MODCRAFT_PERF: enabled (perf instrumentation compiled in)")
endif()

# Two games share one C++ engine (src/platform). Each game is a set of
# executables under src/<Game>/, linking the platform's source tree.
#
#   src/platform/   Engine: shared types, server loop, agent runtime, renderer
#   src/ModCraft/   Voxel sandbox game (chunks, blocks, structures)
#   src/EvolveCraft/ Spore-cell-stage game (cells, fluid field) — stub
#
# Each game's executables run with CWD = $BUILD_DIR, so assets copied into
# $BUILD_DIR by POST_BUILD are found via "shaders/...", "artifacts/...", etc.
# at runtime.

# --- Dependencies via FetchContent ---
include(FetchContent)

# GLM (header-only math — used by both native and web)
FetchContent_Declare(glm URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz)
FetchContent_MakeAvailable(glm)

# Dear ImGui (immediate mode UI — works on native + Emscripten)
FetchContent_Declare(imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.91.8
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_demo.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
if(EMSCRIPTEN)
	target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_OPENGL_ES3)
	target_compile_options(imgui_lib PRIVATE -sUSE_GLFW=3)
else()
	target_link_libraries(imgui_lib PRIVATE glfw)
endif()

if(NOT EMSCRIPTEN)
	FetchContent_Declare(glfw
		GIT_REPOSITORY https://github.com/glfw/glfw.git
		GIT_TAG 3.4
	)
	set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(glfw)

	FetchContent_Declare(glad
		GIT_REPOSITORY https://github.com/Dav1dde/glad.git
		GIT_TAG v2.0.8
		SOURCE_SUBDIR cmake
	)
	FetchContent_MakeAvailable(glad)
	glad_add_library(glad_gl_core_41 REPRODUCIBLE API gl:core=4.1)
endif()

if(NOT EMSCRIPTEN)
	FetchContent_Declare(zstd
		GIT_REPOSITORY https://github.com/facebook/zstd.git
		GIT_TAG v1.5.6
		SOURCE_SUBDIR build/cmake
	)
	set(ZSTD_BUILD_PROGRAMS    OFF CACHE BOOL "" FORCE)
	set(ZSTD_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
	set(ZSTD_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(zstd)
endif()

if(NOT EMSCRIPTEN)
	set(Python3_ROOT_DIR "/usr")
	find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
	FetchContent_Declare(pybind11
		GIT_REPOSITORY https://github.com/pybind/pybind11.git
		GIT_TAG v2.13.6
	)
	FetchContent_MakeAvailable(pybind11)
endif()

if(NOT EMSCRIPTEN)
	find_package(OpenGL REQUIRED)
endif()

# --- Platform engine (src/platform) ---
# Header-only except for select .cpp files. Listed explicitly so CMake knows
# the dependency set.
set(PLATFORM_SERVER_SOURCES
	src/platform/server/server.h
	src/platform/server/server.cpp
	src/platform/server/entity_manager.h
	src/platform/server/behavior.h
	src/platform/server/behavior_store.h
	src/platform/server/client_manager.h
	src/platform/server/pathfind.h
	src/platform/server/python_bridge.h
	src/platform/server/python_bridge.cpp
	src/platform/server/server_tuning.h
	src/platform/server/test_server.h
)

set(PLATFORM_CLIENT_SOURCES
	src/platform/client/window.h
	src/platform/client/window.cpp
	src/platform/client/camera.h
	src/platform/client/camera.cpp
	src/platform/client/shader.h
	src/platform/client/shader.cpp
	src/platform/client/text.h
	src/platform/client/text.cpp
	src/platform/client/model.h
	src/platform/client/model.cpp
	src/platform/client/entity_drawer.h
	src/platform/client/entity_drawer.cpp
	src/platform/client/lightbulb_drawer.h
	src/platform/client/lightbulb_drawer.cpp
	src/platform/client/particles.h
	src/platform/client/particles.cpp
	src/platform/client/controls.h
	src/platform/client/controls.cpp
	src/platform/client/ui.h
	src/platform/client/ui.cpp
	src/platform/client/audio.h
	src/platform/client/audio.cpp
	src/platform/client/types.h
	src/platform/client/game.h
	src/platform/client/game.cpp
	src/platform/client/game_loading.cpp
	src/platform/client/game_playing.cpp
	src/platform/client/game_render.cpp
	src/platform/client/game_ui.cpp
	src/platform/client/gameplay.h
	src/platform/client/gameplay.cpp
	src/platform/client/gameplay_movement.cpp
	src/platform/client/hud.h
	src/platform/client/hud.cpp
	src/platform/client/code_editor.h
	src/platform/client/code_editor.cpp
	src/platform/client/floating_text.h
	src/platform/client/floating_text.cpp
)

# --- ModCraft game sources (src/ModCraft) ---
set(MODCRAFT_CONTENT_SOURCES
	src/ModCraft/content/builtin.h
	src/ModCraft/content/builtin.cpp
)

set(MODCRAFT_CLIENT_SOURCES
	src/ModCraft/client/chunk_mesher.h
	src/ModCraft/client/chunk_mesher.cpp
	src/ModCraft/client/renderer.h
	src/ModCraft/client/renderer.cpp
	src/ModCraft/client/gameplay_interaction.cpp
)

# Include paths: both roots so existing "shared/foo.h" / "server/foo.h"
# / "agent/foo.h" / "client/foo.h" includes resolve transparently.
# File basenames are unique across the platform/ModCraft split.
set(GAME_INCLUDE_DIRS src/platform src/ModCraft)

# Platform + ModCraft server-side sources (shared by all exes that run server logic)
set(FULL_SIM_SOURCES
	${PLATFORM_SERVER_SOURCES}
	${MODCRAFT_CONTENT_SOURCES}
)

# Platform + ModCraft client-side sources (shared by GUI exes)
set(FULL_CLIENT_SOURCES
	${PLATFORM_CLIENT_SOURCES}
	${MODCRAFT_CLIENT_SOURCES}
)

if(EMSCRIPTEN)
	# =================================================================
	# Web build (WASM + WebGL 2) — client-only, connects to remote server
	# =================================================================
	add_executable(modcraft
		src/ModCraft/main.cpp
		${FULL_SIM_SOURCES}
		${FULL_CLIENT_SOURCES}
	)
	target_include_directories(modcraft PRIVATE ${GAME_INCLUDE_DIRS})
	target_link_libraries(modcraft PRIVATE glm::glm imgui_lib)

	set(EM_FLAGS "-sUSE_GLFW=3 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sWEBSOCKET_SUBPROTOCOL=binary")
	set(EM_LINK "--preload-file ${CMAKE_SOURCE_DIR}/src/platform/shaders@shaders --preload-file ${CMAKE_SOURCE_DIR}/src/ModCraft/shaders@shaders --preload-file ${CMAKE_SOURCE_DIR}/src/ModCraft/config@config --preload-file ${CMAKE_SOURCE_DIR}/src/platform/fonts@fonts --preload-file ${CMAKE_SOURCE_DIR}/src/ModCraft/artifacts@artifacts")
	set_target_properties(modcraft PROPERTIES
		SUFFIX ".html"
		COMPILE_FLAGS "${EM_FLAGS}"
		LINK_FLAGS "${EM_FLAGS} ${EM_LINK}"
	)

else()
	# =================================================================
	# Native builds (Linux / Mac / Windows)
	# =================================================================

	# === Singleplayer (server + client in one process) ===
	add_executable(modcraft
		src/ModCraft/main.cpp
		${FULL_SIM_SOURCES}
		${FULL_CLIENT_SOURCES}
	)
	target_include_directories(modcraft PRIVATE ${GAME_INCLUDE_DIRS})
	target_link_libraries(modcraft PRIVATE
		glfw glad_gl_core_41 glm::glm OpenGL::GL
		Python3::Python pybind11::embed
		imgui_lib libzstd_static
	)

	# === Dedicated server (headless, no OpenGL) ===
	add_executable(modcraft-server
		src/ModCraft/main_server.cpp
		${FULL_SIM_SOURCES}
	)
	target_include_directories(modcraft-server PRIVATE ${GAME_INCLUDE_DIRS})
	target_link_libraries(modcraft-server PRIVATE
		glm::glm
		Python3::Python pybind11::embed
		libzstd_static
	)

	# === Network client (connects to remote server) ===
	add_executable(modcraft-client
		src/ModCraft/main_client.cpp
		${FULL_SIM_SOURCES}
		${FULL_CLIENT_SOURCES}
	)
	target_include_directories(modcraft-client PRIVATE ${GAME_INCLUDE_DIRS})
	target_link_libraries(modcraft-client PRIVATE
		glfw glad_gl_core_41 glm::glm OpenGL::GL
		Python3::Python pybind11::embed
		imgui_lib libzstd_static
	)

	# === Headless E2E test runner ===
	add_executable(modcraft-test
		src/ModCraft/test/test_e2e.cpp
		${FULL_SIM_SOURCES}
	)
	target_include_directories(modcraft-test PRIVATE ${GAME_INCLUDE_DIRS})
	target_link_libraries(modcraft-test PRIVATE
		glm::glm
		Python3::Python pybind11::embed
	)

	# --- Asset staging to $BUILD_DIR ---
	# All executables run with CWD=$BUILD_DIR, so POST_BUILD copies assets
	# from the per-game source tree into the build dir with the same
	# "shaders/", "artifacts/", etc. names that the C++ code expects.
	# NOTE: do NOT rm -rf the shaders dir here — EvolveCraft's POST_BUILD
	# copies pond/creature/food shaders into the same dir. Clobbering would
	# break `make game GAME=evolvecraft` on rebuilds where only modcraft's
	# POST_BUILD fires. copy_directory is idempotent and creates it.
	add_custom_command(TARGET modcraft POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/platform/shaders $<TARGET_FILE_DIR:modcraft>/shaders
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/ModCraft/shaders $<TARGET_FILE_DIR:modcraft>/shaders
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/ModCraft/config $<TARGET_FILE_DIR:modcraft>/config
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/ModCraft/python $<TARGET_FILE_DIR:modcraft>/python
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/platform/fonts $<TARGET_FILE_DIR:modcraft>/fonts
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/ModCraft/artifacts $<TARGET_FILE_DIR:modcraft>/artifacts
		COMMAND cp -ru ${CMAKE_SOURCE_DIR}/src/ModCraft/resources $<TARGET_FILE_DIR:modcraft>/
	)

	# modcraft-test needs python/ + artifacts/ in its CWD to load behaviors.
	add_custom_command(TARGET modcraft-test POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/ModCraft/python $<TARGET_FILE_DIR:modcraft-test>/python
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/ModCraft/artifacts $<TARGET_FILE_DIR:modcraft-test>/artifacts
	)

	# =================================================================
	# EvolveCraft — Spore cell stage game.
	# Singleplayer one-process for now. No network, no embedded Python.
	# Reuses Window/Shader from src/platform; everything else is in
	# src/EvolveCraft/.
	# =================================================================
	add_executable(evolvecraft
		src/EvolveCraft/main.cpp
		src/platform/client/window.cpp
		src/platform/client/shader.cpp
		src/platform/client/text.cpp
		src/platform/client/particles.cpp
	)
	target_include_directories(evolvecraft PRIVATE
		src/platform
		src/EvolveCraft
	)
	target_link_libraries(evolvecraft PRIVATE
		glfw glad_gl_core_41 glm::glm OpenGL::GL
	)
	# EvolveCraft shader filenames (pond/creature/food) do not collide with
	# ModCraft's, so we merge them into $BUILD_DIR/shaders without wiping.
	add_custom_command(TARGET evolvecraft POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:evolvecraft>/shaders
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/EvolveCraft/shaders $<TARGET_FILE_DIR:evolvecraft>/shaders
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${CMAKE_SOURCE_DIR}/src/platform/shaders/text.vert
			${CMAKE_SOURCE_DIR}/src/platform/shaders/text.frag
			${CMAKE_SOURCE_DIR}/src/platform/shaders/particle.vert
			${CMAKE_SOURCE_DIR}/src/platform/shaders/particle.frag
			$<TARGET_FILE_DIR:evolvecraft>/shaders/
	)
endif()
