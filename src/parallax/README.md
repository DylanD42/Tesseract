# Tesseract Depth Window

The integrated app now has one presentation mode: smooth, bounded head-tracked
Depth mode. Physical monitor calibration, metric eye-depth tracking and the P
mode switch have been removed. Saved monitor-photo profiles are not loaded or
deleted. The separate original `HeadTracking` application remains available.

## Modules

| File | Responsibility |
| --- | --- |
| `main.cpp` | Command-line options, application lifetime, connect modules |
| `headtracking.cpp/.h` | Camera/backend selection, YuNet face detection, diagnostics, worker thread |
| `tracking_types.h` | Normalized face position, confidence, sample ID and status |
| `window_motion.cpp/.h` | Bounded head movement, fixed viewing distance, smoothing, recentering, saved settings |
| `3Denvironment.cpp/.h` | Window lifecycle, input, projection, render loop |
| `off_axis_projection.cpp/.h` | View/frustum matrices and aspect-correct virtual opening |
| `test_scene.cpp/.h` | Gridded room, lit sculptures and optional solid comparison room |
| `window_controls.cpp/.h` | Live Depth settings and save/reset buttons |
| `ui.cpp/.h` | Optional detailed diagnostics |
| `settings.h` | Default window, scene and detector constants |

## Build and run

From the repository root:

```powershell
cmake --build build/raylib-test --config Release --target TesseractParallax -- /m:1 /nr:false
.\build\raylib-test\bin\Release\TesseractParallax.exe 1
```

Camera index 1 is the established working virtual camera. Omit the index to scan
0-9; an explicit index never silently selects another device. Capture tries
DirectShow, Media Foundation and automatic backend selection, preserving the
native feed format. Start the virtual camera output before launching.

The integrated app needs only `custom/face_detection_yunet_2023mar.onnx`, copied
beside the executable during the build. It does not load dlib, landmarks or a
monitor setup profile. For a camera-free scene/motion check:

```powershell
.\build\raylib-test\bin\Release\TesseractParallax.exe --mouse
```

A fresh Depth-only configuration can disable the independent legacy apps:

```powershell
cmake -S . -B build/depth-only -DBUILD_HEADTRACKING=OFF -DBUILD_RAYLIB_SCENE=OFF -DBUILD_PARALLAX=ON
cmake --build build/depth-only --config Release --target TesseractParallax -- /m:1 /nr:false
```

That configuration does not fetch/link dlib or build the landmark pose application.
The normal default project still includes the standalone legacy applications.

## How the effect works

The approach is informed by [Depth Window](https://arcade.pirillo.com/depth-window.html).
YuNet detects the face box. Its center is normalized to image coordinates and
published as a thread-safe snapshot. Camera rotation, facial landmarks, gaze and
estimated metric depth do not drive the scene.

A detection-stage smoother feeds a frame-time-corrected rendering smoother.
Camera travel is capped at 15% of opening width horizontally and 10% vertically.
Viewing distance stays fixed at five-thirds of opening width, so noisy monocular
Z estimates cannot make the room zoom in and out. Forward/back movement does not
change viewing distance. This is a tuned depth illusion rather than a measurement
of real head-to-screen geometry.

The asymmetric frustum uses the eye position relative to the virtual opening.
The screen-plane corners remain fixed at the viewport edges. The opening follows
window aspect ratio, and the camera keeps a screen-aligned orientation. Five
room grids provide perspective references, while three static, lit sculptures
occupy distinct depths. The optional solid room retains the earlier visual test.

## Controls

| Key | Action |
| --- | --- |
| C or V | Open/close Depth settings |
| R | Recenter using the latest face position |
| M | Switch camera/mouse preview; `--mouse` always uses mouse input |
| B | Compare dark grid and solid test rooms |
| H | Show/hide tracking diagnostics in the 3D window |
| F11 | Borderless fullscreen |
| Escape | Close settings first; otherwise exit |

Settings include sensitivity, smoothing, room depth, grid opacity and grid density.
Save settings persists them in `window_view.txt` beside the executable. Reset
applies defaults immediately; save afterward to retain the reset. C now opens
these useful Depth controls rather than the retired monitor-photo menu.

## Ownership and tests

The main thread owns raylib, menus, scene GPU resources and projection. A worker
owns camera processing and publishes copied snapshots under a mutex. An atomic
flag requests shutdown; destruction joins the worker before releasing capture.
GPU resources are freed before closing the rendering window.

The current app has no reason to compute solvePnP, face landmarks, metric eye
filters or monitor-photo offsets, so those integrated modules and their retired
physical-mode test suites have been removed. Original standalone head-tracking
source and model files remain separate.

```powershell
cmake -S . -B build/calibration-tests -DBUILD_HEADTRACKING=OFF -DBUILD_RAYLIB_SCENE=OFF -DBUILD_PARALLAX=OFF -DBUILD_PARALLAX_TESTS=ON
cmake --build build/calibration-tests --config Release -- /m:1 /nr:false
ctest --test-dir build/calibration-tests -C Release --output-on-failure
```

The existing test directory name is retained. Active suites check projection
anchoring, aspect changes, parallax signs, ray/screen agreement, unit invariance,
fixed depth, movement bounds, duplicate-snapshot handling, refresh-independent
smoothing, recentering and settings persistence. They do not measure actual
camera latency or guarantee a particular perceptual result.
