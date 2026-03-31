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

# --- Find system OpenGL ---
find_package(OpenGL REQUIRED)

# --- Sources ---
set(COMMON_SOURCES
	src/common/types.h
	src/common/block.h
	src/common/chunk.h
	src/common/world.h
)

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
	src/game/player.h
	src/game/game.h
	src/game/game.cpp
	src/game/menu.h
	src/game/menu.cpp
	src/game/gameplay.h
	src/game/gameplay.cpp
	src/game/hud.h
	src/game/hud.cpp
)

# --- Executable ---
add_executable(aicraft
	src/main.cpp
	src/builtin/builtin.cpp
	${CLIENT_SOURCES}
	${GAME_SOURCES}
)

target_include_directories(aicraft PRIVATE
	src
)

target_link_libraries(aicraft PRIVATE
	glfw
	glad_gl_core_41
	glm::glm
	OpenGL::GL
)

# Copy shaders and config to build directory
add_custom_command(TARGET aicraft POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	${CMAKE_SOURCE_DIR}/shaders
	$<TARGET_FILE_DIR:aicraft>/shaders
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	${CMAKE_SOURCE_DIR}/config
	$<TARGET_FILE_DIR:aicraft>/config
)
