"""signals.py — Event kinds emitted by the engine to Behavior.react().

AgentClient emits these kinds to nearby owned agents when it detects
relevant world events (a stranger approached, HP dropped, …). Each signal
carries a `kind` string and a `payload` dict with event-specific fields.

Behaviors dispatch on `signal.kind` — the constants here are the stable
shared vocabulary between the C++ signal source and Python react().

Signals are rate-limited per agent (Agent::kReactCooldownSec) and
anchor-immune: an agent with a live anchor on its current plan skips
react() because the server's anchor aim already re-derives velocity
each tick.
"""

# A non-same-species Living entity moved within alerting distance.
# payload: {"source_id": "<EntityId>", "source_type": "<type_id>"}
THREAT_NEARBY = "threat_nearby"
