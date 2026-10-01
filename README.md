# Tesseract
Adding a 3rd dimension to computer screens using head tracking and off point projection!

Repository for our senior design project! 

## Face pose setup

The tracker uses dlib's 68-point facial landmark predictor and OpenCV `solvePnP`.
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