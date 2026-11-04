# Changes made during this development session

## Starting point

The integrated application was a single `Tesseract_Integrated_Parallax.cpp` combining
camera opening, YuNet detection, dlib landmarks, solvePnP eye-position estimates,
calibration averaging, thread synchronization, off-axis rendering and a floating-
shape scene. The repository also had separate `HeadTracking` and `RaylibScene` apps.

## Camera compatibility

- Preserved virtual-camera native resolution instead of forcing 640x480.
- Added/retained DirectShow, Media Foundation and automatic backend fallback.
- Added automatic camera scanning (0-9), explicit camera-index selection and clearer
  startup messages. OBS Virtual Camera was confirmed to deliver frames at index 1.
- The standalone HeadTracking preview gained Enter selection without requiring a
  detected face, plus N to move to the next camera.

## Modular architecture

- Replaced the integrated monolith with an entry point, tracker, renderer, UI,
  scene, motion controller, projection math and shared snapshot type.
- Kept camera/detection on a worker and rendering on the main thread.
- Added explicit ownership and cleanup: stop/join capture worker, release camera,
  and free GPU resources before closing the rendering context.
- Made model lookup relative to the executable/working directory instead of a
  developer-specific absolute path.
- Updated CMake to compile/link the modules and copy the model each app needs.

## Projection and tracking experiments

- Reviewed Magic-Window's absolute screen-relative projection and added projection
  regression tests. The asymmetric-frustum principle was already present; tests
  now check screen anchoring, movement signs and independent ray intersections.
- Tried photo-based monitor setup, physical lens offsets, pose scale and physical
  eye-to-screen mapping.
- Tried stronger metric filtering, spike rejection and eye-corner-based estimates
  to reduce monocular pose jitter and rotation/translation leakage.
- These physical/metric paths were subsequently retired at the user's request.
  They are not part of the current integrated app. Saved profiles/photos were
  not deleted. The independent original HeadTracking app is still available.

## Current Depth behavior

- Adopted a feel informed by the Depth Window reference: use the face-box center,
  bound horizontal/vertical camera movement, and hold viewing distance constant.
- Added detection and rendering smoothing corrected for frame time.
- Removed forward/back depth pumping and dependence on monitor dimensions, lens
  placement, generic face scale, gaze and head rotation for the integrated effect.
- Added recentering and mouse preview, including `--mouse` startup without a camera
  or tracking model.
- Removed the P physical-mode switch, monitor-photo menu, metric landmark/solvePnP
  pipeline and unused physical-only modules/tests. C and V now open Depth settings.
- Dlib is no longer loaded or linked by TesseractParallax, and a Depth-only CMake
  configuration can skip fetching/building it entirely.

## Scene and interface

- First added a solid room with floor tiles, walls, a doorway, depth-separated
  objects, lighting and approximate contact shadows.
- Added the current dark, five-surface grid room with three lit trefoil sculptures
  at different depths. B retains the solid-room comparison.
- Added sensitivity, smoothing, room depth, grid opacity and grid density controls
  with save/reset and `window_view.txt` persistence.
- Hid the large HUD by default; H shows diagnostics. Kept fullscreen and added
  clear keyboard hints and menu-aware Escape behavior.

## Build and validation

The executable name and established build/run commands remain the same. CMake
now compiles multiple source files. Active camera-free suites cover projection
math and Depth movement/smoothing/settings. Retired physical-only suites were
removed with the features they tested. Unit tests do not replace live checks of
camera latency, tracking quality or the visual illusion.
