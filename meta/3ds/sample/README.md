# Nintendo 3DS sample

A whole game, small enough to read, for checking the 3DS export end to end.
The 3DS runs the engine, so unlike the SEGA samples this is not a scene to be
converted but a game to be carried across as it is -- scripts and all.

| File | What it is |
| --- | --- |
| `Resources/GameConfig.xml` | the title, developer and identifier that end up in the SMDH, a 400x240 display -- the top screen -- and the scene to start in |
| `Resources/Scenes/sample.tmx` | the Mega Drive sample's map, with one object on it: a `Viewer` |
| `Resources/Sounds/chime.wav` | the chime the HOME Menu banner plays, played again on A |
| `Scripts/Viewer.hsl` | moves the camera, with the D-pad or on its own, and draws the platform name and a frame count in the built-in font |

Between them they touch most of what a game leans on: the scene and its tiles,
the script VM, controller input, sound, translucent drawing and text. The frame
count is drawn every frame, so two screenshots a moment apart always differ,
which is how a still frame is told from a running game.

```sh
HatchGameEngine --project-dir meta/3ds/sample \
                --export-3ds HatchSample.3dsx \
                --3ds-runtime build/HatchGameEngine.3dsx
python3 tools/verify-3ds-export.py HatchSample.3dsx \
                meta/3ds/sample/Resources build/HatchGameEngine.3dsx
```

Exporting compiles the script into `Resources/Objects`, which is why that folder
is ignored. The **Nintendo 3DS** section of the repository's README covers the
rest, including turning the `.3dsx` into a `.cia`.
