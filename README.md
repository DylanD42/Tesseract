# Tesseract

Adding a 3rd dimension to computer screens using head tracking and off-axis projection.
Senior design project.

A webcam tracks your eyes (YuNet face box -> dlib 68 landmarks -> solvePnP), and raylib draws a 3D scene
through an asymmetric frustum so the monitor behaves like a window into the scene.

## Build and run

Requirements: Visual Studio 2026 (C++ desktop workload), CMake 3.20+, git. Network access on the first
configure (dlib is fetched; raylib and OpenCV are git submodules).

```
git submodule update --init --recursive
cmake --preset windows-msvc
cmake --build --preset release
.\build\bin\Release\Tesseract.exe        # optional argument: camera index (default 0)
```

For a clean build run

```
Remove-Item -Recurse -Force .\build
```


The first build compiles OpenCV, dlib and raylib from source and takes a while. After that only the
Tesseract sources rebuild. `cmake --build --preset debug` builds the Debug configuration.

The face models live in `custom/` and are copied next to the executable, so it runs from any working
directory. If you run from the IDE without that copy, the source `custom/` folder is used.

## Using it

Sit still for a second after start so the tracker can center on you. Keys on the scene screen:

| Key          | Action                                                      |
|--------------|-------------------------------------------------------------|
| `1`-`5`, TAB | Switch scene                                                |
| `[` `]`      | Parallax strength (1.00 = physically exact)                 |
| `P`          | True geometry (needs the screen photo) / recentered on you  |
| `R`          | Recenter                                                    |
| `L`          | Latency prediction on/off                                   |
| `H`          | HUD on/off                                                  |
| `C`          | Setup menu: screen photo, webcam field of view              |
| `F11`        | Borderless fullscreen                                       |
| `ESC`        | Quit                                                        |

Setup results are saved to `tesseract_calibration.yml` in the working directory. Without the setup, the
screen size comes from the monitor (EDID) and the webcam is assumed to be centered just above the screen
with a 70 degree horizontal field of view.

A second window shows the webcam with the detected landmarks for diagnostics.

### Scene selector

Scene names are listed on the left. Click one to preview it behind the menu: in the preview the **mouse** stands
in for your head (move it to look around). **Open scene** (or double-click the name) enters the scene, and head
tracking takes over. Before anything is picked, the default background shows. The panel on the right shows the
scene's description. Imported scenes also have **Scene settings**: allow rotation, studio backdrop, auto-play
animations, edit description, and delete (click twice). These are saved per scene in `models/library.yml`.

### Importing your own 3D models

In **Select Scene**, click **+ Load a scene...** or drag a file onto the window. Supported: `.glb`, `.gltf`,
`.obj`, `.iqm`, `.vox`, `.m3d`. The file (plus any `.bin`, texture or `.mtl` files it references) is copied into
`models/` next to `tesseract_calibration.yml` and listed in `models/library.yml`, so it stays in the scene list
after a restart. The title is the file name. After the import, the scene selector previews it and asks for an
optional description.

From Blender: **File > Export > glTF 2.0 (.glb)** and tick **Include > Cameras**. Raw `.blend` files can't be read.

- **With a camera in the file**, the Blender camera becomes your eye at your sitting position, at real scale
  (1 Blender metre = 1 real metre). An object 40 cm in front of the Blender camera floats 40 cm in front of your
  eyes, so it pops out of the screen if you sit further away than that. Use the camera's position and rotation
  to stage the scene. Its focal length is ignored, because the physical screen sets the field of view.
- **Without a camera**, the model is scaled to fit the window and set just behind the glass. You see it from
  Blender's Front view (Numpad 1: looking from -Y toward +Y).
- **Spin** (if allowed in the scene settings): click and drag on the scene to turn the model (left/right) and tilt it toward you (up/down).
  A flick keeps it spinning for a moment. Double-click resets it. The whole file turns about its center.
- **Animations** play with a Play/Pause button, clip arrows and a timeline slider along the bottom while the
  mouse moves. Space toggles play. Each Blender action is one clip. Supported: object keyframes (location,
  rotation, scale; Linear, Constant and Bezier, with parenting), armature animations, and objects parented to
  bones. Not supported: shape keys and camera animation. Raylib plays the bone animations; the object keyframes
  are read from the glTF by `render/model_library.cpp`, because raylib's loader ignores them.
- **Studio backdrop** (scene settings): files with a camera get a
  checkered floor in the Blender world under the lowest object, lit from above. Files without one get a grid box
  room around the window. Turn the backdrop off when your Blender scene has its own environment.

## Code layout

```
src/
  main.cpp               wiring only: load settings, start the tracker, run the frame loop
  core/
    config.h/.cpp        constants + Config (what is saved to tesseract_calibration.yml)
    app_state.h/.cpp     AppState: everything the UI can change, and how changes reach the tracker/renderer
    assets.h/.cpp        finds the model files in custom/
    text.h/.cpp          Fmt(): printf-style std::string formatting
  tracking/
    head_tracker.h/.cpp  HeadTracker: webcam + YuNet + dlib + solvePnP + filtering on its own threads
  render/
    viewer.h/.cpp        physical screen -> world units, eye smoothing, off-axis projection, fullscreen
    scene_shader.*       lighting/fog shader
    scene_textures.*     procedural textures
    scene_helpers.*      drawing helpers shared by the scenes
    scenes.h/.cpp        the scene list (SceneDef table + imported models) and the per-frame 3D render
    model_library.*      imported models: copy into models/, library.yml, GPU loading, glTF camera, animation
  ui/
    ui_layer.h           UiLayer interface: update() + draw(); UiFrame with per-frame read-only info
    ui_widgets.*         NumberField, DrawPanel, DrawLinesAt
    hotkeys.*            keyboard shortcuts
    hud.*                the status overlay and the toast
    setup_ui.*           setup menu, screen photo, FOV entry
```

### Adding UI (scene selector, settings tab, ...)

1. Create `src/ui/my_panel.h/.cpp` with a class that derives from `UiLayer` (see `ui/ui_layer.h`;
   `ui/hud.cpp` is the smallest example, `ui/setup_ui.cpp` a complete one).
2. In `update()`, read input and change `AppState`. Use its actions (`selectScene`, `setStrength`,
   `setFov`, `togglePhysicalAlignment`, `requestSave`) or set its fields (`showHud`, `prediction`,
   `fullscreen`, `recenter`...). Return `true` when your panel used the mouse/keyboard so the hotkeys
   do not fire as well.
3. In `draw()`, draw with normal raylib 2D calls. Window size is `frame.geo.sw` x `frame.geo.sh`.
   Scene names come from `SceneName(i)` / `SceneCount()` in `render/scenes.h`.
4. Add the .cpp to `CMakeLists.txt` and the layer to the `layers` vector in `main.cpp`
   (last in the list = drawn on top, gets input first).

Saving, pushing new webcam parameters to the tracker and toggling fullscreen all happen in `main.cpp`
from the flags in `AppState`; UI code never calls the tracker or the renderer.

### Adding a scene

Write `void SceneMyThing(const SceneCtx&)` in `render/scenes.cpp` and add a row to the `kScenes` table.
The scene is drawn inside the off-axis projection with the lighting shader bound; `SceneCtx` gives the
opening rectangle in world units, the light direction, the textures, the time and `unitsPerMm`.

## Tracking notes

The tracker estimates camera intrinsics from the frame size and the webcam field of view (default 70
degrees horizontal, zero lens distortion). The real webcam FOV may differ, so enter it in the setup
menu (the readout there shows the measured eye distance: too short, lower the FOV; too long, raise it). Positions are in millimetres based on a generic 3D face
template, so facial proportions and head pose affect accuracy.
