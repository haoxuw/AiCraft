"""Generated from C++ headers — do NOT edit by hand.
Regenerate with:  python3 tools/gen_protocol_py.py
Sources:
    src/platform/net/net_protocol.h  (PROTOCOL_VERSION, MsgType)
    src/platform/logic/action.h       (ActionProposal::Type)
"""

PROTOCOL_VERSION = 9

ENTITY_NONE      = 0

# ── MsgType (net_protocol.h) ────────────────────────────────────────
C_ACTION         = 0x0001
C_HELLO          = 0x0003
C_GET_INVENTORY  = 0x000D
C_QUIT           = 0x000E
C_HEARTBEAT      = 0x000F
C_PING           = 0x0010
S_WELCOME        = 0x1001
S_ENTITY         = 0x1002
S_CHUNK          = 0x1003
S_REMOVE         = 0x1004
S_TIME           = 0x1005
S_BLOCK          = 0x1006
S_INVENTORY      = 0x1007
S_ERROR          = 0x100B
S_CHUNK_EVICT    = 0x100E
S_CHUNK_Z        = 0x100F
S_READY          = 0x1012
S_PREPARING      = 0x1013
S_NPC_INTERRUPT  = 0x1010
S_WORLD_EVENT    = 0x1011
S_ANNOTATION_SET = 0x1014
S_WEATHER        = 0x1015
S_ENTITY_DELTA   = 0x1016
S_PONG           = 0x1017
S_BLOCK_BATCH    = 0x1018

# ── ActionProposal::Type (action.h) ─────────────────────────────────
TYPE_MOVE     = 0
TYPE_RELOCATE = 1
TYPE_CONVERT  = 2
TYPE_INTERACT = 3
