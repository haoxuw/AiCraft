"""Core types used throughout the game model."""

from __future__ import annotations
from dataclasses import dataclass
import math


@dataclass(frozen=True)
class Vec3:
    """3D vector / position with float precision."""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

    def __add__(self, other: Vec3) -> Vec3:
        return Vec3(self.x + other.x, self.y + other.y, self.z + other.z)

    def __sub__(self, other: Vec3) -> Vec3:
        return Vec3(self.x - other.x, self.y - other.y, self.z - other.z)

    def __mul__(self, s: float) -> Vec3:
        return Vec3(self.x * s, self.y * s, self.z * s)

    def length(self) -> float:
        return math.sqrt(self.x ** 2 + self.y ** 2 + self.z ** 2)

    def normalized(self) -> Vec3:
        ln = self.length()
        if ln < 1e-9:
            return Vec3(0, 0, 0)
        return self * (1.0 / ln)

    def distance(self, other: Vec3) -> float:
        return (self - other).length()

    def to_block_pos(self) -> BlockPos:
        return BlockPos(int(math.floor(self.x)),
                        int(math.floor(self.y)),
                        int(math.floor(self.z)))

    def to_tuple(self) -> tuple[float, float, float]:
        return (self.x, self.y, self.z)


@dataclass(frozen=True)
class BlockPos:
    """Integer block coordinate."""
    x: int = 0
    y: int = 0
    z: int = 0

    def offset(self, dx: int, dy: int, dz: int) -> BlockPos:
        return BlockPos(self.x + dx, self.y + dy, self.z + dz)

    def to_vec3(self) -> Vec3:
        return Vec3(self.x + 0.5, self.y + 0.5, self.z + 0.5)

    def to_tuple(self) -> tuple[int, int, int]:
        return (self.x, self.y, self.z)


# Opaque entity identifier, assigned by the server.
EntityId = int
