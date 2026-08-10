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
| **Scenes** | Browses the project's scene list by category, shows the resolved path of the selected scene, and loads it. Also lists scene files found under `Resources/Scenes`. |
| **Play** | Pause, step frame by frame, fast forward, restart the scene, recompile scripts, or restart the engine. Toggles hitboxes, object regions, tile collision and the performance overlay, and shows live object, tileset, view and layer information with per-layer visibility switches. |
| **Settings** | Fullscreen, V-Sync, window size, master/music/sound volume, log level, preferred renderer and the developer hotkeys — written back to `config.ini` with **Save Settings**. |
| **Console** | The engine log inside the window, coloured by severity, filterable, and following new messages as they arrive. |
| **Help** | Engine and renderer information, and the current developer key bindings. |

The game is paused while the editor is open, so anything you inspect holds
still; turn that off under **Play** if you would rather watch it run. The
`[studio]` section of `config.ini` holds the editor's own preferences:

```ini
[studio]
openOnStart=true    ; open the editor as soon as the engine starts
pauseWhenOpen=true  ; hold the game still while the editor is on screen

[keys]
studio=F12          ; key that opens and closes the editor
```

## Documentation

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
