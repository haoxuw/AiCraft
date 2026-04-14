# CellCraft — Spore-cell-stage drawing game. See docs/00_OVERVIEW.md.
#
# M0 scope: one binary, `cellcraft`, that opens a window, renders a
# chalkboard, and lets the mouse draw chalk strokes. No server split, no
# Python, no physics yet — just the shader beachhead. Server/agent
# binaries come in M1+.

set(CELLCRAFT_INCLUDE_DIRS
	${CMAKE_SOURCE_DIR}/src/platform
	${CMAKE_SOURCE_DIR}/src
)

# Platform bits the client needs at the bare minimum: the GL window +
# shader loader. Explicitly listed rather than pulled from FULL_CLIENT_SOURCES
# so a misconfigured CivCraft build can't fail CellCraft.
set(CELLCRAFT_PLATFORM_SOURCES
	${CMAKE_SOURCE_DIR}/src/platform/client/window.cpp
	${CMAKE_SOURCE_DIR}/src/platform/client/shader.cpp
	${CMAKE_SOURCE_DIR}/src/platform/client/text.cpp
)

# Sim library — headless authoritative simulation. No GL, no Python.
# Lives under src/CellCraft/sim; see docs/00_OVERVIEW.md § Action types.
add_library(cellcraft_sim STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/sim/sim.cpp
)
target_include_directories(cellcraft_sim PUBLIC ${CELLCRAFT_INCLUDE_DIRS})
target_link_libraries(cellcraft_sim PUBLIC glm::glm)

add_executable(cellcraft
	${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/client/chalk_renderer.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/client/app.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/client/game_log.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/client/part_render.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/client/lab_screen.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/client/lab_gizmo.cpp
	${CELLCRAFT_PLATFORM_SOURCES}
)
target_include_directories(cellcraft PRIVATE ${CELLCRAFT_INCLUDE_DIRS})
target_link_libraries(cellcraft PRIVATE
	cellcraft_sim
	glfw glad_gl_core_41 glm::glm OpenGL::GL
)

# Headless smoke test for the sim. No GL, no window.
add_executable(cellcraft-sim-test
	${CMAKE_CURRENT_SOURCE_DIR}/tests/sim_test.cpp
)
target_include_directories(cellcraft-sim-test PRIVATE ${CELLCRAFT_INCLUDE_DIRS})
target_link_libraries(cellcraft-sim-test PRIVATE cellcraft_sim glm::glm)

# Stage shaders alongside the binary — code loads via `shaders/foo.vert`
# relative to CWD (`build/`).
add_custom_command(TARGET cellcraft POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:cellcraft>/shaders
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:cellcraft>/shaders
	# Also stage shared platform shaders (text.* needed by platform TextRenderer).
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/src/platform/shaders $<TARGET_FILE_DIR:cellcraft>/shaders
)
