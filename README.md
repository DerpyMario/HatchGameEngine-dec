# HatchGameEngine
Multiplatform engine powering projects and making ideas into reality.

I don't actively push to this, but this is here for portfolio reasons. Feel free to look around; however, this may not build without the correct libraries.

## Hatch Studio (built-in editor)

The engine ships with a graphical editor, so making and running a game no longer
means a command line, a text editor and a restart. Start the executable with no
arguments and it opens by itself; otherwise press **F12** (or **`**) at any time
to bring it up over the running game, and **Escape** to dismiss it. It is drawn
by the engine's own renderer and needs no extra libraries or asset files.

| Tab | What it does |
| --- | --- |
| **Project** | Lists every game folder and `.hatch` file next to the engine, plus recently opened ones. Open one with a click, or scaffold a new project (`Resources` tree and a starter `GameConfig.xml`) and open it straight away. |
| **Scenes** | Browses the project's scene list by category, shows the resolved path of the selected scene, and loads it. Also lists scene files found under `Resources/Scenes`, and creates new ones. |
| **Editor** | The scene editor. See below. |
| **Collision** | The tile collision editor. See below. |
| **3D** | The project's models, their materials, and its shaders. See below. |
| **Resources** | Browses the project's `Resources` tree with each file's kind and size, loads scenes from it, and tracks whether the open scene has unsaved changes. |
| **Play** | Pause, step frame by frame, fast forward, restart the scene, recompile scripts, or restart the engine. Toggles hitboxes, object regions, tile collision and the performance overlay, and shows live object, tileset, view and layer information, a breakdown of where the frame went, and per-layer visibility switches. |
| **Settings** | Fullscreen, V-Sync, window size, master/music/sound volume, log level, preferred renderer and the developer hotkeys — written back to `config.ini` with **Save Settings**. |
| **Console** | The engine log inside the window, coloured by severity, filterable, and following new messages as they arrive. |
| **Help** | Engine and renderer information, and the current developer key bindings. |

Along the top is a menu bar in the shape of the one
[HatchStudio](https://github.com/HatchGameEngine/HatchStudio) uses:

- **File** — New Project (`Ctrl+Alt+N`), New Scene (`Ctrl+N`), Open Project
  (`Ctrl+Alt+O`), Open Data File (`Ctrl+O`), a Recent Projects submenu, Close
  Project (`Ctrl+Alt+W`), Save Settings (`Ctrl+S`), Exit.
- **Project** — Run (`Ctrl+R`), Restart Scene (`F6`), Recompile Scripts & Reload
  Scene (`F5`), Restart Engine (`F1`), a Set Run Start Scene radio group
  (run from the game's start scene, or from whichever scene is loaded), Open
  Scene File, Rescan Project.
- **View** — jump to any tab, and toggle pausing, the performance overlay and
  fullscreen.
- **Help** — the Help tab and Close Editor (`F12`).

Open Project, Open Data File, Open Scene File and the scripts folder Browse
button all open a file browser drawn by the engine, so a project anywhere on
disk can be opened without a command line. Text fields take `Ctrl+C`, `Ctrl+X`
and `Ctrl+V`.

## Making a scene

**New Scene**, under the Scenes tab or in the File menu, writes a scene into the
project and opens it, so a level can be started without Tiled and without
copying another one. Give it a name, a size in tiles, a tile size and how many
layers it wants, and it lands in `Resources/Scenes/<name>/<name>.tmx`, which is
where the engine looks for a scene's tile collision alongside it.

The scene that is loaded lends the new one its tilesets, since a new level is
nearly always another level for the same game -- and it is the only way to know
a tileset's tile size and count without going and reading the image. The
reference written out is relative, so no image is copied. With no scene loaded
the map is still written, just without tilesets, and the layers are there to be
painted once one is added.

What comes out is an ordinary Tiled map: `Resources/Scenes/MyScene/MyScene.tmx`
opens in Tiled and reads back into the engine unchanged.

## Scene editor

The **Editor** tab edits the scene that is loaded, in place. The game keeps
rendering behind it and the editor draws its grid, selection and markers over
the top, so every change shows the moment it is made. The game is held still
while the editor is open.

- **Paint** and **Erase** lay down the selected tile with an adjustable brush,
  optionally flipped, and optionally forcing either collision plane.
- **Pick** takes a tile and its flip flags from the scene into the brush.
- **Select** drags out a rectangle, which **Capture** turns into a reusable
  stamp; **Stamp** lays stamps back down, skipping their empty tiles.
- **Entity** picks an entity out of the scene and drags it around, with a
  Snap To Tile Grid for tidying up.
- **Layers** lists every layer with its size and offsets, chooses which one the
  brush works on, and toggles each one's visibility.
- Everything is undoable. A whole brush stroke undoes in one step.

**Save Scene** writes the tile layers back to the scene file. Only the `<data>`
element of each layer is rewritten, so tilesets, object groups, custom
properties and anything else in the file survive untouched; loading a scene,
saving it and loading it again gives back a byte-identical file. Tiled `.tmx`
scenes can be written back today. Scenes inside a packed `.hatch` file cannot be
saved, and the Save button says so.

Anything that would discard unsaved scene edits -- opening another project,
closing one, loading a different scene -- asks first, offering to save, discard
or cancel.

## Tile collision editor

The **Collision** tab is the editor
[HatchTileCollisionEditor](https://github.com/HatchGameEngine/HatchStudio) is,
inside the engine and working on the collision the running game is using.

A collision file gives each tile sixteen column heights, a ceiling flag and an
angle; everything the engine actually collides against -- which sides are solid,
the angle of each side, the three flipped copies of the tile -- is worked out
from those when the file loads. The editor edits the heights and asks the engine
to work the rest out again through the same routine the loader uses, so a tile
drawn here behaves exactly like the same tile loaded from a file.

- The tile is drawn magnified with its own graphic underneath it, so collision
  can be laid against the art it is meant to match. Clicking a square gives that
  column its surface; clicking the same square again takes the column away.
- **Fill**, **Clear** and **Half** do a whole tile at once.
- **Ceiling tile** turns the tile over, so its columns hang down from the top.
- **Angle** is the tile's ground angle, shown in degrees as well as in the
  0--255 the file stores.
- Hatch scenes have two collision planes, A and B. The **Plane** dropdown
  chooses which one is being drawn on and **Copy To Plane B** copies across.
- The tile picker marks every tile that already has collision, so a tileset can
  be worked through without guessing.

**Save Tile Collision** writes the scene's collision file back out in the same
`TCOL` format the engine reads, covering the first tileset the way the loader
expects. Loading a collision file, saving it and loading it again gives back a
byte-identical file. As with scenes, collision inside a packed `.hatch` file
cannot be written back, and the button says so.

## 3D

The engine has had 3D in it all along -- models in its own `.hmdl`, `.md3` and
RSDK formats plus whatever Open Asset Import reads when it is built in,
materials, lighting, fog, and a renderer for all of it -- but the only way to
reach any of it was from a script, so a game had to be written before anything
could be looked at. The **3D** tab puts it in front of you.

- **Models** lists every model in the project, loads one, and says what is in
  it: meshes and their vertex counts, materials, animations, armatures, and
  whether it animates by vertices or by bones.
- **Materials** lists the loaded model's materials and edits one: its diffuse,
  specular, ambient and emissive colours, its shininess and opacity, and the
  textures it names.
- **Shaders** finds every shader in the project and builds it. **Rebuild** picks
  up an edit without restarting, and a shader that stops compiling leaves the
  working one in use rather than dropping it.
- **3D Scene** shows the camera, lighting and fog of whatever 3D scene the game
  has set up.

### Shaders

A shader is a `.vert` and a `.frag` that share a name, anywhere under
`Resources`. It talks to the engine through the same names the engine's own
shaders use, so it only has to declare the parts it wants:

```glsl
attribute vec3 i_position;   uniform mat4 u_projectionMatrix;
attribute vec2 i_uv;         uniform mat4 u_modelViewMatrix;
attribute vec4 i_color;      uniform vec4 u_color;
                             uniform sampler2D u_texture;
                             uniform sampler2D u_paletteTexture;
                             uniform vec4 u_fogColor;
                             uniform float u_fogLinearStart, u_fogLinearEnd;
                             uniform float u_fogDensity;
```

From a script:

```js
var shader = Shader.Load("Shaders/tint.vert", "Shaders/tint.frag");
Shader.Set(shader);
// ... draw ...
Shader.Unset();
```

`Shader.Load` gives back -1 if the shader did not build, and the console carries
whatever the driver said about it. Only the OpenGL renderer builds shaders from
source; the others say so rather than pretending.

The game is paused while the editor is open, so anything you inspect holds
still; turn that off under **View** or **Play** if you would rather watch it
run. The `[studio]` section of `config.ini` holds the editor's own preferences:

```ini
[studio]
openOnStart=true         ; open the editor as soon as the engine starts
pauseWhenOpen=true       ; hold the game still while the editor is on screen
runFromStartScene=true   ; what Project > Run loads
recent0=../SomeGame      ; recently opened projects

[keys]
studio=F12               ; key that opens and closes the editor
```

## Command line

The engine takes the same options as
[upstream Hatch](https://github.com/HatchGameEngine/HatchGameEngine), so
shortcuts and scripts written for either one work here:

```
HatchGameEngine [options] [scene file]

  --project-dir <path>    Run the game in <path>, which holds its
                          Resources and Scripts folders.
  --resource-file <path>  Read resources from a packed .hatch file.
  --scripts-dir <path>    Read scripts from <path> instead of Scripts.
  --scene <path>          Load this scene at startup, relative to
                          the Resources folder.
  --studio                Open the editor on startup.
  --help                  Show this text.
```

A single argument that is not an option is still treated as a `.hatch` file or
as a scene file inside a `Resources` folder, the way the engine has always been
started from a file manager.

## Documentation

## Builds

The **Build application** workflow under Actions is started by hand and builds
one self-contained application for every target:

| Artifact | What it is |
| --- | --- |
| `HatchGameEngine-windows-x64` | A single `.exe` |
| `HatchGameEngine-windows-arm64` | A single `.exe` |
| `HatchGameEngine-macos-universal` | A `.app` holding one binary with both Apple silicon and Intel in it |
| `HatchGameEngine-linux-x86_64` | A single executable |
| `HatchGameEngine-linux-aarch64` | A single executable |

Each job builds SDL2 from source as a static library and links it, along with
GLEW and the C++ runtime, into the executable, so what comes out asks nothing of
the machine it lands on beyond what the operating system already has. Windows
uses the static C runtime, so no Visual C++ redistributable is needed either.
The Unix artifacts arrive as tarballs because a zip -- which is what GitHub
wraps artifacts in -- does not carry the executable bit.

## Building
### Windows
Included in /VisualC is a Visual Studio 2019 solution. You'll need the x86 version of the [Microsoft Visual C++ Redistributable for Visual Studio 2015, 2017 and 2019](https://support.microsoft.com/en-us/topic/the-latest-supported-visual-c-downloads-2647da03-1eea-4433-9aff-95f26a218cc0) installed to compile in Visual Studio.

## Dependecies
Required:
- SDL2 (https://www.libsdl.org/)
- Visual C++ (for Windows building)
- Android Studio (for Android building)
- Xcode 12 (for iOS building)
- devKitPro (for Nintendo Switch/3DS homebrew building) (wip)

Optional:
- [Open Asset Import Library](https://github.com/assimp/assimp)
- FFmpeg (for video playback, currently broken)
- CURL (for simple HTTP network, currently broken)
- libpng
- libjpeg
- libogg
- libfreetype
