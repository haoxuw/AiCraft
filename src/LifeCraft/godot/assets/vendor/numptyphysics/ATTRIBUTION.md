# NumptyPhysics asset attribution

These files are imported from **NumptyPhysics** — an open-source 2D
physics puzzle game by Tim Edmonds and Thomas Perl.

- Source:  https://github.com/midzer/numptyphysics
- License: **GPL-3.0** — see `LICENSE-GPL3.txt` in this directory.

Because of the GPL-3 viral clause, any LifeCraft build that ships
`paper.png` is distributed under GPL-3. Strip this directory if a
more permissive license is required.

## Imported files

| Path              | Purpose in LifeCraft                          |
|-------------------|-----------------------------------------------|
| `paper.png`       | Faded-paper background behind the chalkboard/sketch pad look |

## Techniques adopted (NOT copied code — re-implemented in GDScript)

- Stroke rendering style: feathered ribbon (opaque core + transparent
  outer edges). NumptyPhysics source at
  `platform/gl/GLRenderer.cpp:757-835`. We reproduce the look using a
  Godot `Line2D` with a `transparent→opaque→transparent` gradient.
- Color palette: `src/Colour.cpp:21-34`.
- Path simplification threshold of 1.0 pixel (Douglas-Peucker).
- `PIXELS_PER_METRE = 10` constant for Box2D scaling.
