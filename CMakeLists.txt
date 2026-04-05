cmake_minimum_required(VERSION 3.20)
project(Agentica VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies via FetchContent ---
include(FetchContent)

# GLM (header-only math — used by both native and web)
FetchContent_Declare(glm
	URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
)
FetchContent_MakeAvailable(glm)

# Dear ImGui (immediate mode UI — works on native + Emscripten)
FetchContent_Declare(imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.91.8
)
FetchContent_MakeAvailable(imgui)

# Build ImGui as a library with GLFW + OpenGL3 backends
add_library(imgui_lib STATIC
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_demo.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
	${imgui_SOURCE_DIR}
	${imgui_SOURCE_DIR}/backends
)
if(EMSCRIPTEN)
	target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_OPENGL_ES3)
	target_compile_options(imgui_lib PRIVATE -sUSE_GLFW=3)
else()
	target_link_libraries(imgui_lib PRIVATE glfw)
endif()

# Native-only dependencies (Emscripten provides its own GLFW + GL)
if(NOT EMSCRIPTEN)
	# GLFW
	FetchContent_Declare(glfw
		GIT_REPOSITORY https://github.com/glfw/glfw.git
		GIT_TAG 3.4
	)
	set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(glfw)

	# GLAD (OpenGL loader)
	FetchContent_Declare(glad
		GIT_REPOSITORY https://github.com/Dav1dde/glad.git
		GIT_TAG v2.0.8
		SOURCE_SUBDIR cmake
	)
	FetchContent_MakeAvailable(glad)
	glad_add_library(glad_gl_core_41 REPRODUCIBLE API gl:core=4.1)
endif()

# miniaudio: single-header audio library, placed directly in src/client/miniaudio.h
# No FetchContent needed — CC0 licensed, downloaded from github.com/mackron/miniaudio

# pybind11 (Python embedding — required for behavior system, native only)
if(NOT EMSCRIPTEN)
	set(Python3_ROOT_DIR "/usr")
	find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
	FetchContent_Declare(pybind11
		GIT_REPOSITORY https://github.com/pybind/pybind11.git
		GIT_TAG v2.13.6
	)
	FetchContent_MakeAvailable(pybind11)
endif()

# --- Find system OpenGL (native only — Emscripten provides its own) ---
if(NOT EMSCRIPTEN)
	find_package(OpenGL REQUIRED)
endif()

# --- Sources ---
# Shared: pure data types, linked by both server and client
set(SHARED_SOURCES
	src/shared/types.h
	src/shared/constants.h
	src/shared/block_registry.h
	src/shared/chunk.h
	src/shared/entity.h
	src/shared/inventory.h
	src/shared/action.h
	src/shared/physics.h
	src/shared/box_model.h
	src/shared/character.h
	src/shared/face.h
)

# Server: authoritative world simulation
set(SERVER_SOURCES
	src/server/world.h
	src/server/entity_manager.h
	src/server/behavior.h
	src/server/noise.h
	src/server/world_template.h
)

# Content: data-driven definitions (linked by server)
set(CONTENT_SOURCES
	src/content/builtin.h
	src/content/builtin.cpp
	src/content/blocks_terrain.h
	src/content/blocks_plants.h
	src/content/blocks_active.h
	src/content/entities_animals.h
	src/content/entities_player.h
	src/content/entities_items.h
)

# Client: rendering + input + game UI
set(CLIENT_SOURCES
	src/client/window.h
	src/client/window.cpp
	src/client/camera.h
	src/client/camera.cpp
	src/client/shader.h
	src/client/shader.cpp
	src/client/chunk_mesher.h
	src/client/chunk_mesher.cpp
	src/client/renderer.h
	src/client/renderer.cpp
	src/client/text.h
	src/client/text.cpp
	src/client/model.h
	src/client/model.cpp
	src/client/particles.h
	src/client/particles.cpp
	src/client/controls.h
	src/client/controls.cpp
	src/client/ui.h
	src/client/ui.cpp
	src/client/audio.h
	src/client/audio.cpp
	src/client/types.h
	src/client/game.h
	src/client/game.cpp
	src/client/gameplay.h
	src/client/gameplay.cpp
	src/client/gameplay_movement.cpp
	src/client/gameplay_interaction.cpp
	src/client/hud.h
	src/client/hud.cpp
	src/client/code_editor.h
	src/client/code_editor.cpp
)

# Server simulation sources
set(SERVER_SIM_SOURCES
	src/server/server.h
	src/server/server.cpp
	src/server/python_bridge.h
	src/server/python_bridge.cpp
)

if(EMSCRIPTEN)
	# =================================================================
	# Web build (WASM + WebGL 2) — client-only, connects to remote server
	# =================================================================
	add_executable(agentica
		src/main.cpp
		${CONTENT_SOURCES}
		${SERVER_SIM_SOURCES}
		${CLIENT_SOURCES}
	)
	target_include_directories(agentica PRIVATE src)
	target_compile_definitions(agentica PRIVATE AGENTICA_WEB=1)
	target_link_libraries(agentica PRIVATE glm::glm imgui_lib)

	# Emscripten compile + link flags
	set(EM_FLAGS "-sUSE_GLFW=3 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576")
	set(EM_LINK "--preload-file ${CMAKE_SOURCE_DIR}/shaders@shaders --preload-file ${CMAKE_SOURCE_DIR}/config@config --preload-file ${CMAKE_SOURCE_DIR}/fonts@fonts --preload-file ${CMAKE_SOURCE_DIR}/artifacts@artifacts")
	set_target_properties(agentica PROPERTIES
		SUFFIX ".html"
		COMPILE_FLAGS "${EM_FLAGS}"
		LINK_FLAGS "${EM_FLAGS} ${EM_LINK}"
	)

else()
	# =================================================================
	# Native builds (Linux / Mac / Windows)
	# =================================================================

	# === Singleplayer (server + client in one process) ===
	add_executable(agentica
		src/main.cpp
		${CONTENT_SOURCES}
		${SERVER_SIM_SOURCES}
		${CLIENT_SOURCES}
	)
	target_include_directories(agentica PRIVATE src)
	target_link_libraries(agentica PRIVATE
		glfw glad_gl_core_41 glm::glm OpenGL::GL
		Python3::Python pybind11::embed
		imgui_lib
	)

	# === Dedicated server (headless, no OpenGL) ===
	add_executable(agentica-server
		src/main_server.cpp
		${CONTENT_SOURCES}
		${SERVER_SIM_SOURCES}
	)
	target_include_directories(agentica-server PRIVATE src)
	target_link_libraries(agentica-server PRIVATE
		glm::glm
		Python3::Python pybind11::embed
	)

	# === Network client (connects to remote server) ===
	add_executable(agentica-client
		src/main_client.cpp
		${CONTENT_SOURCES}
		${SERVER_SIM_SOURCES}
		${CLIENT_SOURCES}
	)
	target_include_directories(agentica-client PRIVATE src)
	target_link_libraries(agentica-client PRIVATE
		glfw glad_gl_core_41 glm::glm OpenGL::GL
		Python3::Python pybind11::embed
		imgui_lib
	)

	# === Headless agent client (AI entity controller, no OpenGL) ===
	add_executable(agentica-bot
		src/agent/main_bot.cpp
		src/server/python_bridge.cpp
		${CONTENT_SOURCES}
	)
	target_include_directories(agentica-bot PRIVATE src)
	target_link_libraries(agentica-bot PRIVATE
		glm::glm
		Python3::Python pybind11::embed
	)
	# Copy python/ and artifacts/ so behavior loading works
	add_custom_command(TARGET agentica-bot POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/python $<TARGET_FILE_DIR:agentica-bot>/python
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/artifacts $<TARGET_FILE_DIR:agentica-bot>/artifacts
	)

	# === Headless E2E test runner ===
	add_executable(agentica-test
		src/test/test_e2e.cpp
		${CONTENT_SOURCES}
		${SERVER_SIM_SOURCES}
	)
	target_include_directories(agentica-test PRIVATE src)
	target_link_libraries(agentica-test PRIVATE
		glm::glm
		Python3::Python pybind11::embed
	)
	# Copy python/ and artifacts/ so behavior loading finds them (even if Python fails gracefully)
	add_custom_command(TARGET agentica-test POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/python $<TARGET_FILE_DIR:agentica-test>/python
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/artifacts $<TARGET_FILE_DIR:agentica-test>/artifacts
	)

	# Copy assets to build directory
	add_custom_command(TARGET agentica POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:agentica>/shaders
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/config $<TARGET_FILE_DIR:agentica>/config
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/python $<TARGET_FILE_DIR:agentica>/python
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/fonts $<TARGET_FILE_DIR:agentica>/fonts
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/artifacts $<TARGET_FILE_DIR:agentica>/artifacts
		COMMAND cp -ru ${CMAKE_SOURCE_DIR}/resources $<TARGET_FILE_DIR:agentica>/
	)
endif()
