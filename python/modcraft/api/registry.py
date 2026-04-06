"""ArtifactRegistry -- how user-created content gets into the game.

Players write Python code extending the base classes, test locally,
then upload to the server. The registry validates, sandboxes, and
registers their code so it becomes live in the world.

STATUS: STUB. See ModCraft/16_ARTIFACT_REGISTRY_TODO.md for full design.
"""

from __future__ import annotations
from typing import Any
from modcraft.api.base import Object, ObjectMeta, ActiveObject, PassiveObject


class ArtifactInfo:
    """Metadata about a registered user artifact."""

    def __init__(self, artifact_id: str, author: str, version: int,
                 obj_class: type, meta: ObjectMeta,
                 source_code: str = ""):
        self.artifact_id = artifact_id   # "alice:flying_pig"
        self.author = author             # "alice"
        self.version = version           # auto-incremented
        self.obj_class = obj_class       # the Python class
        self.meta = meta                 # ObjectMeta instance
        self.source_code = source_code   # original .py source


class ArtifactRegistry:
    """Manages built-in and user-uploaded object/action definitions.

    Built-in content is registered at startup.
    User content is hot-loaded during gameplay.

    TODO: Full implementation in milestone (see 16_ARTIFACT_REGISTRY_TODO.md)
    - Sandbox validation (AST scan, import whitelist)
    - Resource limits (CPU timeout, memory cap)
    - Hot-reload (replace class, migrate instances)
    - Asset bundling (textures, models, sounds)
    - Version history + rollback
    - Attribution chain (forking)
    - Network broadcast to clients on registration
    """

    def __init__(self):
        self._objects: dict[str, ArtifactInfo] = {}
        self._actions: dict[str, ArtifactInfo] = {}

    # --- Registration (used at startup for built-ins) ---

    def register_block_meta(self, meta: ObjectMeta,
                            obj_class: type = PassiveObject,
                            author: str = "system") -> None:
        """Register a block type from an ObjectMeta + class pair."""
        self._objects[meta.id] = ArtifactInfo(
            artifact_id=meta.id,
            author=author,
            version=1,
            obj_class=obj_class,
            meta=meta,
        )

    def register_action(self, action_class: type,
                        author: str = "system") -> None:
        """Register an action type."""
        meta = action_class.meta
        self._actions[meta.id] = ArtifactInfo(
            artifact_id=meta.id,
            author=author,
            version=1,
            obj_class=action_class,
            meta=meta,
        )

    # --- Lookup ---

    def get_object(self, type_id: str) -> ArtifactInfo | None:
        return self._objects.get(type_id)

    def get_action(self, type_id: str) -> ArtifactInfo | None:
        return self._actions.get(type_id)

    def get_class(self, type_id: str) -> type | None:
        info = self._objects.get(type_id) or self._actions.get(type_id)
        return info.obj_class if info else None

    def create_instance(self, type_id: str, **kwargs: Any) -> Object | None:
        """Create an object instance from a registered type."""
        info = self._objects.get(type_id)
        if not info:
            return None
        return info.obj_class(meta=info.meta, **kwargs)

    def list_objects(self) -> list[str]:
        return list(self._objects.keys())

    def list_actions(self) -> list[str]:
        return list(self._actions.keys())

    # --- User artifact upload (STUB) ---

    def upload_artifact(self, source_code: str, author: str,
                        assets: dict[str, bytes] | None = None
                        ) -> tuple[bool, str]:
        """Validate and register a user-uploaded artifact.

        Args:
            source_code: Python source defining the object/action class
            author: Player name
            assets: Optional dict of {filename: binary_data} for textures etc.

        Returns:
            (success, message) tuple

        TODO: Implement (see 16_ARTIFACT_REGISTRY_TODO.md)
        """
        raise NotImplementedError(
            "Artifact upload not yet implemented. "
            "See ModCraft/16_ARTIFACT_REGISTRY_TODO.md"
        )

    def hot_reload(self, artifact_id: str, new_source: str
                   ) -> tuple[bool, str]:
        """Replace a registered artifact's class with updated code.

        TODO: Implement (see 16_ARTIFACT_REGISTRY_TODO.md)
        """
        raise NotImplementedError(
            "Hot-reload not yet implemented. "
            "See ModCraft/16_ARTIFACT_REGISTRY_TODO.md"
        )
