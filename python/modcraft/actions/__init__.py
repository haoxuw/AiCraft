"""Built-in action definitions."""

from modcraft.actions.player import Mine, Place, Attack
from modcraft.actions.world import GrassSpread, GrassDie

BUILTIN_ACTIONS: dict[str, type] = {}

def _register_all():
    import modcraft.actions.player as _p
    import modcraft.actions.world as _w
    for module in [_p, _w]:
        for name in dir(module):
            cls = getattr(module, name)
            if isinstance(cls, type) and hasattr(cls, 'meta') and hasattr(cls.meta, 'id'):
                BUILTIN_ACTIONS[cls.meta.id] = cls

_register_all()
