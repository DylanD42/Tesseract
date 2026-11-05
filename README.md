# Tesseract
Adding a 3rd dimension to computer screens using head tracking and off point projection!

Repository for our senior design project! 

to build and run the integrated Tesseract app:
cmake --preset windows-msvc
cmake --build --preset release
.\build\combined\bin\Release\TesseractParallax.exe

The separate legacy applications can still be run independently:
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

The integrated app starts full-screen without opening a camera. Select a camera
index under **Settings** and choose **Start camera** to begin tracking. The app
opens only that device, trying DirectShow, Media Foundation and then automatic
backend selection while preserving the camera's default resolution. Start your
virtual camera output before selecting it.

In HeadTracking's camera preview, press Enter to select any working feed, even
before a face is detected. Press N to try the next camera. A visible face is still
needed for pose tracking and calibration.

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

## TesseractParallax scenes

The integrated app uses fixed viewing distance and bounded face-position
movement. Its scene picker uses the two existing raylib scenes: the off-axis
floating-shape scene and the animated square demo. The temporary grid-room and
solid test-room scenes have been removed. Physical monitor calibration, metric
eye-depth tracking and dlib landmarks are not used by the integrated app.

On launch, Tesseract opens a start screen with **Select Scene**, **Settings**,
and **Exit Program**. The mouse stays visible until a scene is selected. Scene
selection offers the existing floating-shape and animated-square scenes; press
Escape during gameplay to return to the start screen.

**C/V** opens settings, **R** recenters, **M** switches camera/mouse input, **H**
shows diagnostics, and **F11** toggles fullscreen. Camera selection, sensitivity
and smoothing can be saved beside the executable.

```powershell
cmake --build build/raylib-test --config Release --target TesseractParallax -- /m:1 /nr:false
.\build\raylib-test\bin\Release\TesseractParallax.exe
```

Use `--mouse` for a camera-free session. See
[the architecture guide](src/parallax/README.md) and
[the session change summary](CHANGES.md) for details.
