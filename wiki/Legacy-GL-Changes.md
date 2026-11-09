# Legacy GL changes (always active)

[Home](Home.md) - File: `src/hardware/hw_main.cpp`

These changes to the Legacy GL renderer are active in Ring Racers E **with or without** Low-End Mode.
Some give exactly the same picture with less work; others change what is drawn.

## Same picture, less work

| Change | Why |
|---|---|
| Float version of `R_PointToAngle64` for walls (`RRE_SlopeF`), same result through `tantoangle`. | The original uses a 64-bit division, which is slow on weak CPUs. |
| Quick backface rejection of walls (side of the seg relative to the camera, like `R_PointOnSegSide`) before computing the two angles. | Most back faces are rejected without any angle math. |
| Sector data that only depends on the view is computed once per sector and view, not per subsector. | About 3 subsectors per sector shared the same work. |
| Sprite roll is done by rotating the quad on the GPU. Rotsprite is only kept for sprites split by lights and for floor sprites. | Rotsprite created a new texture for every angle (about 1000 texture uploads when a race starts, for item boxes). |
| The screen is copied into a texture (`MakeScreenTexture`, in `HWR_DoPostProcessor`) only when the view has a water or heat effect, and not in splitscreen. The original copied it every frame. | That texture is only read by the water/heat redraw in the same frame (`HWR_DrawIntermissionBG` is never called in Ring Racers). The copy made the CPU wait for the GPU: 1 to 17 ms per frame with ANGLE, and some time on every API. |

## Different picture

| Change | Why |
|---|---|
| **Draw distance:** BSP branches farther than 12288 units from the camera are skipped (`HWR_CheckBBox`). Configurable for testing with the `dist` test option. | Big levels sent a lot of far geometry that is barely visible. |
| **Flat sky:** the sky is always one flat quad; the sky dome is not drawn. | The dome cost 5 to 34 ms per frame on slow hardware. |
| **No horizon lines:** horizon line specials are not drawn. | They raised the floor up to the camera height, drawing a huge plane over the track. |
| **Sprite lighting without shaders:** when shaders are off, the light contrast on sprites is halved (for example 200 -> about 138 instead of 75). | With the fallback formula, sprites facing away from the light looked almost black. |
| **Floor sprites on slopes:** see [Small fixes](Small-Fixes.md#floor-sprites-on-slopes). | They were partly hidden under the floor. |

For the original code, see
[`hw_main.cpp` at 49beb6b](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/hardware/hw_main.cpp).
