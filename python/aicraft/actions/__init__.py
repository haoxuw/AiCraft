"""Built-in action definitions."""

from aicraft.actions.player import Mine, Place, Attack
from aicraft.actions.world import GrassSpread, GrassDie

BUILTIN_ACTIONS: dict[str, type] = {}

def _register_all():
    import aicraft.actions.player as _p
    import aicraft.actions.world as _w
    for module in [_p, _w]:
        for name in dir(module):
            cls = getattr(module, name)
            if isinstance(cls, type) and hasattr(cls, 'meta') and hasattr(cls.meta, 'id'):
                BUILTIN_ACTIONS[cls.meta.id] = cls

_register_all()
