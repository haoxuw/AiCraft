# ChalkLife

A 2D chalkboard life-sim: procedurally-drawn chalk creatures amble
around a dark green board, animated as if someone's sketching them
in real time.

## Run

```
godot4 --path src/ChalkLife/godot
```

## Look

- **Board** is near-black with a faint green tint and a low-contrast
  grain so it reads as a chalkboard, not a solid fill.
- **Creatures** are drawn with wobbly hand-strokes: body outline + eyes
  + mouth + four legs. Every stroke is seeded so each creature has its
  own handwriting.
- **Post-process** ChalkLife overlay adds dust grain and occasional
  skip-breaks in the strokes (like a chalk stick that loses contact).

## Layout

```
godot/
  project.godot
  scenes/main.tscn            One scene — black board + post-fx overlay
  shaders/chalk_post.gdshader Dust grain + stroke-break post-process
  scripts/
    chalk_pen.gd              Wobbly line / circle / arc / dot helpers
    chalk_creature.gd         One creature (body, face, 4-leg gait)
    main.gd                   Spawns N creatures with distinct seeds
```

## Staging

- **v0** (now): board + N identical-plan chalk creatures wandering.
- **v1**: species variants (size, eye count, leg count, body shape).
- **v2**: creatures react to each other (flock, flee).
- **v3**: hand that draws new parts in real time.
