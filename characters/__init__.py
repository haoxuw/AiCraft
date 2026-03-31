# AiCraft Character Definitions
#
# Each character is defined as a Python dataclass.
# These files serve as:
#   1. Human-readable source of truth for character designs
#   2. Templates for player-created characters
#   3. Direct runtime loading when Python embedding is added (pybind11)
#
# For now, the C++ builtins in src/builtin/characters.h mirror these
# definitions. When pybind11 is integrated, these .py files will be
# loaded directly by the engine.
