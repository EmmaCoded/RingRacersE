# Static geometry cache

[Home](Home.md) - Files: `src/hardware/hw_main.cpp` (namespace `rre_static`), `src/hardware/rr_staticgeo.cpp`,
`src/hardware/r_opengl/r_opengl.cpp` (`DrawStaticPool`)

Only used with [Low-End Mode](Low-End-Mode.md) **and** the Legacy GL renderer.

## The problem

Every frame, Legacy GL walks the BSP and, for each visible floor, ceiling and wall, recomputes its polygon
(heights, texture coordinates, lighting, colormap) and sends it to the GPU one polygon at a time
([`HWR_ProcessSeg`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/hardware/hw_main.cpp#L1023)
and the plane code). Most of the level never changes, so on a slow CPU this is a lot of repeated work.

## How the cache works

1. **Capture:** the first time a subsector floor/ceiling or a wall (seg) is drawn, the polygons produced by the
   normal code are copied into a persistent vertex pool (`rr_staticgeo.cpp`), with the lighting baked in as a
   per-vertex color.
2. **Signature:** each captured item stores a "signature" of everything that affects it (heights, textures, light
   level, offsets, colormap, flags...).
3. **Reuse:** on the next frames, if the signature did not change, all the HWR work for that item is skipped and
   only its indices are added to a list.
4. **Draw:** at the end of the BSP pass, everything is drawn grouped by (texture, flags). The pool lives in a VBO
   that is only re-uploaded when the pool changed (`s_ver`).

Small changes are patched instead of recaptured: a plane that only moved up or down gets its new height, and a
simple wall (two-sided, no slopes, lights or FOFs) gets its new heights in place. Walls with static slopes are
allowed when the sector only has top/bottom walls.

Things that stay on the normal path:
- items that changed 4 times or more (moving platforms, animated lights);
- translucent walls, fog, horizon and no-depth surfaces;
- surfaces without a texture or with more than 255 vertices.

The per-sector view data cache that this code relies on is always active, also without Low-End Mode:
see [Legacy GL changes](Legacy-GL-Changes.md).

## Limits

- The pool holds 65535 vertices (24 bytes each, about 1.5 MB). When it is full, new items use the normal path.
- If the per-level arrays cannot be allocated, the cache is disabled for that level (slower, but no crash).
- The VBOs belong to the GL context: on a live API switch they are released and rebuilt
  (see [Graphics APIs](Graphics-APIs.md#live-switching)).

Turn it off for testing with the `nostatic` test option ([Developer options](Developer-Options.md)).
