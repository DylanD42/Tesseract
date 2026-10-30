# Tesseract
Adding a 3rd dimension to computer screens using head tracking and off point projection!

Repository for our senior design project! 

to build and run the project:
cmake --preset windows-msvc
cmake --build --preset release
.\build\combined\bin\Release\HeadTracking.exe
.\build\combined\bin\Release\RaylibScene.exe


to build only one program 
cmake --build --preset headtracking
cmake --build --preset raylib


to clean the workplace for a clean build
cmake --build --preset release --target clean

## Virtual cameras

The integrated app is organized into modules under `src/parallax`.
See [the architecture guide](src/parallax/README.md) for module responsibilities,
communication, build instructions, tests, and planned extension points.

Start your virtual camera output before launching the app. Physical and virtual
cameras are opened through DirectShow first, then Media Foundation and automatic
backend selection. Allow a few seconds for the feed to start.

In HeadTracking's camera preview, press Enter to select any working feed, even
before a face is detected. Press N to try the next camera. A visible face is still
needed for pose tracking and calibration.

The integrated app tries indexes 0-9 automatically until a feed opens. To select
a specific device, pass the camera index, for example:
`.\build\combined\bin\Release\TesseractParallax.exe 1`.
It preserves the camera's default resolution for virtual camera compatibility.

## Standalone HeadTracking pose setup

The separate `HeadTracking.exe` uses dlib's 68-point facial landmark predictor and OpenCV `solvePnP`.
The integrated `TesseractParallax.exe` uses only YuNet face-center detection.
On the first run, download dlib's `shape_predictor_68_face_landmarks.dat.bz2`,
decompress it, and place `shape_predictor_68_face_landmarks.dat` in the repository's
`custom` directory. CMake fetches dlib during the first configure, so that configure
requires network access.

The app does not require checkerboard calibration. It estimates camera intrinsics from
the selected frame size and assumes a 70-degree horizontal field of view with zero lens
distortion. This lets it start immediately, but the actual webcam field of view may
differ, so estimated distances are approximate.

After startup, hold still while the app averages 30 face-pose frames to set the
starting position. `FacePositionState` stores `initialPosition`, `currentPosition`, and
`relativePosition` as XYZ vectors. The display reports movement relative to the starting
position and current estimated Z. Press R to set a new starting position. Values are in
millimeters based on a generic 3D face template, so camera FOV, individual facial
proportions, and head pose affect accuracy. A depth camera or calibrated intrinsics
would provide more reliable measurements.

## Depth Window app

The integrated app now has one smooth Depth mode: fixed viewing distance, bounded
face-position movement and a dark grid room with three lit sculptures. Physical
mode and its photo-calibration menu have been removed; old saved monitor profiles
are left on disk but no longer affect rendering. The integrated app no longer
runs dlib landmarks, solvePnP or metric eye-depth estimation.

**C/V** opens Depth settings, **R** recenters, **M** switches mouse preview, **B**
compares rooms, **H** shows diagnostics, and **F11** toggles fullscreen. Settings
for sensitivity, smoothing and room appearance can be saved beside the executable.

```powershell
cmake --build build/raylib-test --config Release --target TesseractParallax -- /m:1 /nr:false
.\build\raylib-test\bin\Release\TesseractParallax.exe 1
```

Use `--mouse` instead of `1` for camera-free testing. See
[the architecture guide](src/parallax/README.md) and
[the session change summary](CHANGES.md) for details.
