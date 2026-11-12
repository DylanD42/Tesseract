# Tesseract

Adds a 3rd dimension to computer screens using head tracking and off-axis projection. Senior design project.

A webcam tracks your eyes (YuNet face box -> dlib 68 landmarks -> solvePnP), and raylib draws a 3D scene
through an asymmetric frustum, so the monitor behaves like a window into the scene.

## Build and run

Requirements: Visual Studio 2026 (C++ desktop workload), CMake 3.20+, git. The first configure needs network
access (dlib is fetched; raylib and OpenCV are git submodules). Run the commands below from the repository root.

```
git submodule update --init --recursive
cmake --preset windows-msvc
cmake --build --preset release
.\build\bin\Release\Tesseract.exe        # optional argument: camera index (default 0)
```

- `cmake --build --preset debug` builds the Debug configuration.
- The first build compiles OpenCV, dlib and raylib from source and is slow; later builds only rebuild Tesseract.
- For a clean build, run `Remove-Item -Recurse -Force .\build`.
- Face models in `custom/` are copied next to the executable, so it runs from any working directory. Running
  from the IDE without that copy falls back to the source `custom/` folder.

## Using it

Sit still for a second after start so the tracker can center on you. Scene screen keys:

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

Setup results are saved to `tesseract_calibration.yml` in the working directory. Without setup, the screen
size comes from the monitor (EDID) and the webcam is assumed to sit centered just above the screen with a
70° horizontal field of view.

A second window shows the webcam with detected landmarks for diagnostics.

### Scene selector

- Click a scene name on the left to preview it. In preview, the **mouse** stands in for your head.
- **Open scene** (or double-click) enters the scene and hands control to head tracking.
- The right panel shows the scene description. Imported scenes also have **Scene settings** (allow rotation,
  studio backdrop, auto-play animations, edit description, delete, which needs two clicks). Settings are saved
  per scene in `models/library.yml`.

### Importing 3D models

In **Select Scene**, click **+ Load a scene...** or drag a file onto the window. Supported formats: `.glb`,
`.gltf`, `.obj`, `.iqm`, `.vox`, `.m3d`. The file and any `.bin`, texture or `.mtl` files it references are
copied into `models/` next to `tesseract_calibration.yml` and listed in `models/library.yml`, so they survive
a restart. The title is the file name; after import you can add an optional description.

From Blender, use **File > Export > glTF 2.0 (.glb)** with **Include > Cameras** ticked. Raw `.blend` files
cannot be read.

- **With a camera in the file**, the Blender camera becomes your eye at your sitting position, at real scale
  (1 Blender metre = 1 real metre). An object 40 cm in front of the camera appears 40 cm in front of your eyes,
  so it pops out of the screen if you sit farther away than that. Its focal length is ignored; the physical
  screen sets the field of view.
- **Without a camera**, the model is scaled to fit the window and placed just behind the glass. View it from
  Blender's Front view (Numpad 1, looking from -Y toward +Y).
- **Spin** (if allowed in scene settings): drag to turn the model (left/right) and tilt it toward you (up/down).
  A flick keeps it spinning briefly; double-click resets. The whole file rotates about its center.
- **Animations** have Play/Pause, clip arrows and a timeline slider along the bottom, shown while the mouse
  moves. Space toggles play. Each Blender action is one clip. Supported: object keyframes (location, rotation,
  scale; Linear, Constant and Bezier; parenting), armature animations, and objects parented to bones.
  Not supported: shape keys and camera animation. Raylib plays bone animations; object keyframes are read from
  the glTF by `render/model_library.cpp` because raylib's loader ignores them.
- **Studio backdrop** (scene settings): with a camera, a checkered floor is added under the lowest object, lit
  from above. Without one, a grid box room is drawn around the window. Turn it off if your Blender scene has its
  own environment.

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

1. Create `src/ui/my_panel.h/.cpp` deriving from `UiLayer` (see `ui/ui_layer.h`; `ui/hud.cpp` is the smallest
   example, `ui/setup_ui.cpp` a complete one).
2. In `update()`, read input and change `AppState` via its actions (`selectScene`, `setStrength`, `setFov`,
   `togglePhysicalAlignment`, `requestSave`) or fields (`showHud`, `prediction`, `fullscreen`, `recenter`).
   Return `true` if your panel consumed the mouse/keyboard so hotkeys don't also fire.
3. In `draw()`, use normal raylib 2D calls. Window size is `frame.geo.sw` x `frame.geo.sh`. Scene names come
   from `SceneName(i)` / `SceneCount()` in `render/scenes.h`.
4. Add the .cpp to `CMakeLists.txt` and the layer to the `layers` vector in `main.cpp` (last = drawn on top,
   gets input first).

Saving, pushing webcam parameters to the tracker, and toggling fullscreen happen in `main.cpp` from `AppState`
flags. UI code never calls the tracker or renderer directly.

### Adding a scene

Write `void SceneMyThing(const SceneCtx&)` in `render/scenes.cpp` and add a row to the `kScenes` table. The
scene is drawn inside the off-axis projection with the lighting shader bound. `SceneCtx` provides the opening
rectangle (world units), light direction, textures, time and `unitsPerMm`.

## Tracking notes

Camera intrinsics are estimated from the frame size and the webcam FOV (default 70° horizontal, zero lens
distortion). If your webcam's real FOV differs, enter it in the setup menu. Its readout shows the measured eye
distance: if too short, lower the FOV; if too long, raise it. Positions are in millimetres based on a generic
3D face template, so facial proportions and head pose affect accuracy.
