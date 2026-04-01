#pragma once

/**
 * Python bridge — embeds CPython via pybind11 for behavior execution.
 *
 * This is the connection between C++ server simulation and Python
 * game content. All behaviors, actions, and item definitions are
 * Python code loaded and executed through this bridge.
 *
 * Lifecycle:
 *   1. PythonBridge::init() — starts interpreter, adds python/ to path
 *   2. loadBehavior(code) — compiles Python source, returns handle
 *   3. callDecide(handle, worldView) — calls decide(), returns actions
 *   4. PythonBridge::shutdown() — stops interpreter
 *
 * The bridge runs on the SERVER only. Client never executes Python
 * game logic (it only renders based on entity properties).
 */

#include "shared/entity.h"
#include "shared/action.h"
#include "server/behavior.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace aicraft {

using BehaviorHandle = int;

class PythonBridge {
public:
	// Start the Python interpreter. Call once at startup.
	bool init(const std::string& pythonPath = "python");

	// Stop the interpreter. Call once at shutdown.
	void shutdown();

	bool isInitialized() const { return m_initialized; }

	// Load a behavior from Python source code string.
	// Returns a handle for calling decide() later.
	// On error: returns -1, errorOut contains the traceback.
	BehaviorHandle loadBehavior(const std::string& sourceCode, std::string& errorOut);

	// Call decide() on a loaded behavior.
	// Provides a WorldView context with nearby entities and block info.
	// Returns the BehaviorAction the Python code chose.
	// On error: returns Idle, errorOut contains the traceback.
	// Block info passed to Python behaviors
	struct NearbyBlock {
		int x, y, z;
		std::string typeId;
		float distance;
	};

	BehaviorAction callDecide(BehaviorHandle handle,
	                           Entity& self,
	                           const std::vector<NearbyEntity>& nearby,
	                           const std::vector<NearbyBlock>& nearbyBlocks,
	                           float dt,
	                           std::string& goalOut,
	                           std::string& errorOut);

	// Get the source code of a loaded behavior
	std::string getSource(BehaviorHandle handle) const;

	// Unload a behavior (free Python objects)
	void unloadBehavior(BehaviorHandle handle);

private:
	bool m_initialized = false;
	int m_nextHandle = 1;

	struct LoadedBehavior {
		std::string source;
		// pybind11 objects stored as opaque pointers (impl in .cpp)
		void* moduleObj = nullptr;
		void* instanceObj = nullptr;
	};

	std::unordered_map<int, LoadedBehavior> m_behaviors;
};

// Global bridge instance (owned by server)
PythonBridge& pythonBridge();

} // namespace aicraft
