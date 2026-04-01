cmake_minimum_required(VERSION 3.20)
project(AiCraft VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies via FetchContent ---
include(FetchContent)

# GLFW
FetchContent_Declare(glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM (header-only math)
FetchContent_Declare(glm
	URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
)
FetchContent_MakeAvailable(glm)

# GLAD (OpenGL loader) - using glad2 with cmake integration
FetchContent_Declare(glad
	GIT_REPOSITORY https://github.com/Dav1dde/glad.git
	GIT_TAG v2.0.8
	SOURCE_SUBDIR cmake
)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl_core_41 REPRODUCIBLE API gl:core=4.1)

# pybind11 (Python embedding — required for behavior system)
set(Python3_ROOT_DIR "/usr")
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
FetchContent_Declare(pybind11
	GIT_REPOSITORY https://github.com/pybind/pybind11.git
	GIT_TAG v2.13.6
)
FetchContent_MakeAvailable(pybind11)

# --- Find system OpenGL ---
find_package(OpenGL REQUIRED)

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
	src/content/models.h
	src/content/characters.h
	src/content/faces.h
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
)

set(GAME_SOURCES
	src/game/types.h
	src/game/game.h
	src/game/game.cpp
	src/game/menu.h
	src/game/menu.cpp
	src/game/gameplay.h
	src/game/gameplay.cpp
	src/game/hud.h
	src/game/hud.cpp
	src/game/code_editor.h
	src/game/code_editor.cpp
)

# Server simulation sources
set(SERVER_SIM_SOURCES
	src/server/server.h
	src/server/server.cpp
	src/server/python_bridge.h
	src/server/python_bridge.cpp
)

# === Singleplayer executable (server + client in one process) ===
add_executable(aicraft
	src/main.cpp
	${CONTENT_SOURCES}
	${SERVER_SIM_SOURCES}
	${CLIENT_SOURCES}
	${GAME_SOURCES}
)
target_include_directories(aicraft PRIVATE src)
target_link_libraries(aicraft PRIVATE
	glfw glad_gl_core_41 glm::glm OpenGL::GL
	Python3::Python pybind11::embed
)

# === Dedicated server executable (headless, no OpenGL) ===
add_executable(aicraft-server
	src/main_server.cpp
	${CONTENT_SOURCES}
	${SERVER_SIM_SOURCES}
)
target_include_directories(aicraft-server PRIVATE src)
target_link_libraries(aicraft-server PRIVATE
	glm::glm
	Python3::Python pybind11::embed
)

# === Network client executable (connects to remote server) ===
add_executable(aicraft-client
	src/main_client.cpp
	${CONTENT_SOURCES}
	${SERVER_SIM_SOURCES}
	${CLIENT_SOURCES}
	${GAME_SOURCES}
)
target_include_directories(aicraft-client PRIVATE src)
target_link_libraries(aicraft-client PRIVATE
	glfw glad_gl_core_41 glm::glm OpenGL::GL
	Python3::Python pybind11::embed
)

# Copy shaders, config, and python to build directory
add_custom_command(TARGET aicraft POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:aicraft>/shaders
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	${CMAKE_SOURCE_DIR}/config $<TARGET_FILE_DIR:aicraft>/config
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	${CMAKE_SOURCE_DIR}/python $<TARGET_FILE_DIR:aicraft>/python
)
