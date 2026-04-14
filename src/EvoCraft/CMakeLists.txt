# EvoCraft — C++ server + Godot 4 client.
#
# Only the server is built by CMake. The Godot client lives at
# src/EvoCraft/godot/ and is launched via the root Makefile with
# `make game GAME=evocraft` — it is NOT touched by the C++ build.
#
# M4 scope:
#   evocraft-server — TCP listener that broadcasts 30Hz sim snapshots,
#                     embeds Python via pybind11 and dispatches per-species
#                     decide_batch() every tick.

add_executable(evocraft-server
	server/main.cpp
	server/net_protocol.h
	server/net_server.h
	server/sim.h
	server/swim_slab.h
	server/python_bridge.h
	server/python_bridge.cpp
)

target_include_directories(evocraft-server PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(evocraft-server PRIVATE
	Python3::Python pybind11::embed
)

set_target_properties(evocraft-server PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	# Land the binary at the top-level build dir so `./evocraft-server` works
	# with the same CWD conventions as modcraft / modcraft-server.
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Stage artifacts/ next to the binary so cell species .py files resolve with
# a CWD-relative "artifacts/species/<name>.py" path at runtime.
add_custom_command(TARGET evocraft-server POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_CURRENT_SOURCE_DIR}/artifacts
		${CMAKE_BINARY_DIR}/evocraft_artifacts
)
