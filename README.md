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
- **3D Scene** is a 3D scene being put together: a preview you drag to turn the
  camera around and wheel to move closer, the models placed in it with their
  position, rotation and scale, and the camera and lighting it is lit by.

### 3D scenes

Tile scenes have Tiled and a format that came with it. A 3D scene had neither --
the engine could draw models, light them and fog them, but only because a script
said so at runtime, so there was nothing to save and nothing to open.

**Create 3D Scene** writes one to `Resources/Scenes/<name>.scene3d`, **Add The
Selected Model** puts the model picked in the Models panel into it, and **Save
3D Scene** writes it back. It is XML, like the rest of a Hatch project's
configuration, and holds what the engine needs to put the scene back:

```xml
<scene3d version="1">
 <camera fov="55" near="2" far="9000" yaw="1.25" pitch="0.33" distance="512"
         targetX="10" targetY="20" targetZ="30"/>
 <lighting>
  <ambient r="0.25" g="0.5" b="0.75"/>
  <diffuse r="0.1" g="0.2" b="0.3"/>
  <specular r="0.9" g="0.8" b="0.7"/>
 </lighting>
 <fog equation="1" start="5" end="500" density="0.5" smoothness="0.25"
      r="0.2" g="0.4" b="0.6"/>
 <model source="Models/alpha.hmdl" x="1" y="2" z="3"
        rotationX="0.1" rotationY="0.2" rotationZ="0.3"
        scaleX="1.5" scaleY="2" scaleZ="2.5"/>
</scene3d>
```

Models are named by path and stay where they are, the way a tile scene names its
tilesets. Opening a scene, saving it and opening it again gives back a
byte-identical file. A model that will not load keeps its place in the scene and
is marked, rather than being dropped on the way through.

The preview draws through the engine's own 3D scene -- the same one a game
draws through -- so what shows is what the renderer does with the models, not a
second opinion about them.

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
| `HatchGameEngine-xbox` | A `default.xbe` for the original Xbox |
| `HatchGameEngine-web` | A page -- `index.html`, `index.js`, `index.wasm` -- to drop on any static host |

Each job builds SDL2 from source as a static library and links it, along with
GLEW and the C++ runtime, into the executable, so what comes out asks nothing of
the machine it lands on beyond what the operating system already has. Windows
uses the static C runtime, so no Visual C++ redistributable is needed either.
The Unix artifacts arrive as tarballs because a zip -- which is what GitHub
wraps artifacts in -- does not carry the executable bit.

The Xbox and WebAssembly jobs are the exceptions. Neither builds SDL2 and GLEW:
the Xbox gets them from nxdk, and the browser from Emscripten's ports. What they
produce is a title and a web page rather than an application. See below.

## Building
### Windows
Included in /VisualC is a Visual Studio 2019 solution. You'll need the x86 version of the [Microsoft Visual C++ Redistributable for Visual Studio 2015, 2017 and 2019](https://support.microsoft.com/en-us/topic/the-latest-supported-visual-c-downloads-2647da03-1eea-4433-9aff-95f26a218cc0) installed to compile in Visual Studio.

### Xbox

The original Xbox is built with [nxdk](https://github.com/XboxDev/nxdk), an
open-source toolchain that was written from scratch and shares nothing with
Microsoft's SDK. Nothing from that SDK is needed, used, or supported here.

```sh
git clone --recursive https://github.com/XboxDev/nxdk.git
export NXDK_DIR="$PWD/nxdk"
export PATH="$NXDK_DIR/bin:$PATH"

# nxdk's own libraries: the C++ runtime, SDL2, libpng and zlib.
make -C "$NXDK_DIR" NXDK_ONLY=1 --jobs $(nproc)

cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$NXDK_DIR/share/toolchain-nxdk.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

`build/default.xbe` is the result, named the way a disc or a dashboard expects
to find it. Building it needs clang, lld, llvm, make, bison, flex and cmake on
the machine doing the building.

What the console gets is shaped by what nxdk provides, so some of the engine
does not come along:

| | |
| --- | --- |
| Rendering | The SDL2 backend over the software rasteriser. There is no OpenGL on the Xbox, and the GL renderer is left out of the build entirely. |
| 3D models | No Open Asset Import Library, so the formats it reads are unavailable. The engine's own model formats are unaffected. |
| Polygon triangulation | `Geometry.Triangulate` returns nothing and logs a warning. The library behind it reports bad input by throwing, and nxdk has no exception runtime to catch it with. |
| Networking | `WebSocketClient` is present but never connects. nxdk reaches the network through lwIP, which a title has to bring up itself, and which is not the socket layer the client was written against. |
| Fonts | No FreeType. |
| Saves | Written to `D:\Saves`, on the drive the title was launched from. |

The engine's Xbox code is reached with `#if XBOX`. nxdk defines `_WIN32` as
well, since the console is given a subset of the Windows API, so anywhere the
two differ the Xbox has to be asked about first.

### WebAssembly

The browser is built with [Emscripten](https://emscripten.org/), and renders
through WebGL 2 -- which is OpenGL ES 3.0, a profile the GL renderer already had
a path for.

```sh
git clone --depth 1 https://github.com/emscripten-core/emsdk.git
./emsdk/emsdk install latest
./emsdk/emsdk activate latest
. ./emsdk/emsdk_env.sh

emcmake cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

That writes `build/HatchGameEngine-Release.html` along with the `.js` that loads
it and the `.wasm` itself. All three have to be served together, over HTTP --
opening the file from disk will not work, because a browser will not fetch the
wasm from a `file://` page. Anything that serves static files will do:

```sh
python3 -m http.server --directory build
```

SDL2, libpng and zlib come from Emscripten's ports, so there is nothing to
install for them; the first build downloads and caches them.

To bake a game into the page rather than shipping an empty engine, point
`HATCH_WEB_PRELOAD` at the folder to include. Its contents are mounted at the
root of the page's filesystem, which is where the engine looks:

```sh
emcmake cmake -S . -B build -DHATCH_WEB_PRELOAD=/path/to/MyGame
```

The page itself is `meta/web/shell.html`, and it is a normal HTML file --
editing it changes the page the build produces.

What is different in a browser:

| | |
| --- | --- |
| The frame loop | Driven by the browser through `requestAnimationFrame` rather than by the engine. A page that spun in its own loop would never hand control back, and nothing it drew would reach the screen. |
| Saved games and settings | Kept under `/save`, which the page mounts from IndexedDB, so they survive a reload. Everything else the engine writes goes to a filesystem that lives as long as the tab. |
| `Thread.RunEvent` | Does nothing, and says so. Threads in a browser need SharedArrayBuffer, which needs the host to send `Cross-Origin-Opener-Policy` and `Cross-Origin-Embedder-Policy` headers -- more than a static host can be assumed to do. |
| 3D models | No Open Asset Import Library. The engine's own model formats are unaffected. |
| Fonts | No FreeType. |
| Networking | No `WebSocketClient`. |
| Fog | ES will not index a uniform array with a value that is not constant, so the fog curve is worked out in the shader rather than looked up. It can differ from the desktop table by one step of 256 at a handful of depths; `GLShaderBuilder::BuildFogTableLookup` says exactly where and why. |

The engine's browser code is reached with `#if EMSCRIPTEN`.

## Dependecies
Required:
- SDL2 (https://www.libsdl.org/)
- Visual C++ (for Windows building)
- Android Studio (for Android building)
- Xcode 12 (for iOS building)
- devKitPro (for Nintendo Switch/3DS homebrew building) (wip)
- [nxdk](https://github.com/XboxDev/nxdk) (for original Xbox building)
- [Emscripten](https://emscripten.org/) (for WebAssembly building)

Optional:
- [Open Asset Import Library](https://github.com/assimp/assimp)
- FFmpeg (for video playback, currently broken)
- CURL (for simple HTTP network, currently broken)
- libpng
- libjpeg
- libogg
- libfreetype
