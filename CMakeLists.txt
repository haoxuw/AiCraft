cmake_minimum_required(VERSION 3.20)
project(AiCraft VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Perf instrumentation (frame/tick/handler timing in the dedicated server).
# Enabled only by `make game` via a separate build dir; production builds
# (`make server`, `make client`) leave this off so the code is not compiled in.
option(CIVCRAFT_PERF "Enable server perf instrumentation (frame/tick/handler timing)" OFF)
if(CIVCRAFT_PERF)
	add_compile_definitions(CIVCRAFT_PERF)
	message(STATUS "CIVCRAFT_PERF: enabled (perf instrumentation compiled in)")
endif()

# Two games share one C++ engine (src/platform). Each game is a set of
# executables under src/<Game>/, linking the platform's source tree.
#
#   src/platform/   Engine: shared types, server loop, agent runtime, renderer
#   src/CivCraft/   Voxel sandbox game (chunks, blocks, structures)
#   src/CellCraft/   Spore-cell-stage game — C++ client on src/platform/
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
	src/platform/server/chunk_gen_service.h
	src/platform/server/chunk_gen_service.cpp
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

# --- CivCraft game sources (src/CivCraft) ---
set(CIVCRAFT_CONTENT_SOURCES
	src/CivCraft/content/builtin.h
	src/CivCraft/content/builtin.cpp
)

set(CIVCRAFT_CLIENT_SOURCES
	src/CivCraft/client/chunk_mesher.h
	src/CivCraft/client/chunk_mesher.cpp
	src/CivCraft/client/renderer.h
	src/CivCraft/client/renderer.cpp
	src/CivCraft/client/gameplay_interaction.cpp
)

# Include paths: both roots so existing "shared/foo.h" / "server/foo.h"
# / "agent/foo.h" / "client/foo.h" includes resolve transparently.
# File basenames are unique across the platform/CivCraft split.
set(GAME_INCLUDE_DIRS src/platform src/CivCraft)

# Platform + CivCraft server-side sources (shared by all exes that run server logic)
set(FULL_SIM_SOURCES
	${PLATFORM_SERVER_SOURCES}
	${CIVCRAFT_CONTENT_SOURCES}
)

# Platform + CivCraft client-side sources (shared by GUI exes)
set(FULL_CLIENT_SOURCES
	${PLATFORM_CLIENT_SOURCES}
	${CIVCRAFT_CLIENT_SOURCES}
)

if(EMSCRIPTEN)
	# =================================================================
	# Web build (WASM + WebGL 2) — client-only, connects to remote server
	# =================================================================
	add_executable(civcraft
		src/CivCraft/main.cpp
		${FULL_SIM_SOURCES}
		${FULL_CLIENT_SOURCES}
	)
	target_include_directories(civcraft PRIVATE ${GAME_INCLUDE_DIRS})
	target_link_libraries(civcraft PRIVATE glm::glm imgui_lib)

	set(EM_FLAGS "-sUSE_GLFW=3 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sWEBSOCKET_SUBPROTOCOL=binary")
	set(EM_LINK "--preload-file ${CMAKE_SOURCE_DIR}/src/platform/shaders@shaders --preload-file ${CMAKE_SOURCE_DIR}/src/CivCraft/shaders@shaders --preload-file ${CMAKE_SOURCE_DIR}/src/CivCraft/config@config --preload-file ${CMAKE_SOURCE_DIR}/src/platform/fonts@fonts --preload-file ${CMAKE_SOURCE_DIR}/src/CivCraft/artifacts@artifacts")
	set_target_properties(civcraft PROPERTIES
		SUFFIX ".html"
		COMPILE_FLAGS "${EM_FLAGS}"
		LINK_FLAGS "${EM_FLAGS} ${EM_LINK}"
	)

else()
	# =================================================================
	# Native builds (Linux / Mac / Windows)
	# =================================================================

	# === Singleplayer (server + client in one process) ===
	add_executable(civcraft
		src/CivCraft/main.cpp
		${FULL_SIM_SOURCES}
		${FULL_CLIENT_SOURCES}
	)
	target_include_directories(civcraft PRIVATE ${GAME_INCLUDE_DIRS})
	target_link_libraries(civcraft PRIVATE
		glfw glad_gl_core_41 glm::glm OpenGL::GL
		Python3::Python pybind11::embed
		imgui_lib libzstd_static
	)

	# === Dedicated server (headless, no OpenGL) ===
	add_executable(civcraft-server
		src/CivCraft/main_server.cpp
		${FULL_SIM_SOURCES}
	)
	target_include_directories(civcraft-server PRIVATE ${GAME_INCLUDE_DIRS})
	target_link_libraries(civcraft-server PRIVATE
		glm::glm
		Python3::Python pybind11::embed
		libzstd_static
	)

	# === Network client (connects to remote server) ===
	add_executable(civcraft-client
		src/CivCraft/main_client.cpp
		${FULL_SIM_SOURCES}
		${FULL_CLIENT_SOURCES}
	)
	target_include_directories(civcraft-client PRIVATE ${GAME_INCLUDE_DIRS})
	target_link_libraries(civcraft-client PRIVATE
		glfw glad_gl_core_41 glm::glm OpenGL::GL
		Python3::Python pybind11::embed
		imgui_lib libzstd_static
	)

	# === Headless E2E test runner ===
	add_executable(civcraft-test
		src/CivCraft/test/test_e2e.cpp
		${FULL_SIM_SOURCES}
	)
	target_include_directories(civcraft-test PRIVATE ${GAME_INCLUDE_DIRS})
	target_link_libraries(civcraft-test PRIVATE
		glm::glm
		Python3::Python pybind11::embed
		libzstd_static
	)

	# --- Asset staging to $BUILD_DIR ---
	# All executables run with CWD=$BUILD_DIR, so POST_BUILD copies assets
	# from the per-game source tree into the build dir with the same
	# "shaders/", "artifacts/", etc. names that the C++ code expects.
	add_custom_command(TARGET civcraft POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/platform/shaders $<TARGET_FILE_DIR:civcraft>/shaders
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/CivCraft/shaders $<TARGET_FILE_DIR:civcraft>/shaders
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/CivCraft/config $<TARGET_FILE_DIR:civcraft>/config
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/CivCraft/python $<TARGET_FILE_DIR:civcraft>/python
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/platform/fonts $<TARGET_FILE_DIR:civcraft>/fonts
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/CivCraft/artifacts $<TARGET_FILE_DIR:civcraft>/artifacts
		COMMAND cp -ru ${CMAKE_SOURCE_DIR}/src/CivCraft/resources $<TARGET_FILE_DIR:civcraft>/
	)

	# === Model editor (standalone model viewer / snapshot tool) ===
	# Shared by CivCraft + CellCraft — renders any .py box model without
	# launching a world, server, or full client. Snapshot mode writes
	# 6-angle PPMs to disk (front/three_q/side/back/top/rts) and exits.
	add_executable(model-editor
		src/platform/tools/model_editor/main.cpp
		src/platform/client/shader.h
		src/platform/client/shader.cpp
		src/platform/client/model.h
		src/platform/client/model.cpp
	)
	target_include_directories(model-editor PRIVATE src/platform)
	target_link_libraries(model-editor PRIVATE
		glfw glad_gl_core_41 glm::glm OpenGL::GL
	)
	# Stage the highlight shader so model-editor can find it at runtime.
	add_custom_command(TARGET model-editor POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:model-editor>/shaders
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${CMAKE_SOURCE_DIR}/src/platform/shaders/highlight.vert
			${CMAKE_SOURCE_DIR}/src/platform/shaders/highlight.frag
			$<TARGET_FILE_DIR:model-editor>/shaders/
	)

	# civcraft-test needs python/ + artifacts/ in its CWD to load behaviors.
	add_custom_command(TARGET civcraft-test POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/CivCraft/python $<TARGET_FILE_DIR:civcraft-test>/python
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_SOURCE_DIR}/src/CivCraft/artifacts $<TARGET_FILE_DIR:civcraft-test>/artifacts
	)

	# =================================================================
	# CellCraft — Spore cell stage game (C++ on platform/).
	# M0: single `cellcraft` binary — chalkboard + mouse draws strokes.
	# M1+: server/agent process split once networking lands.
	# The legacy Godot prototype at src/CellCraft/godot/ is retained as a
	# visual reference only and is NOT built by CMake.
	# =================================================================
	if(EXISTS ${CMAKE_SOURCE_DIR}/src/CellCraft/CMakeLists.txt)
		add_subdirectory(src/CellCraft)
	endif()
endif()
