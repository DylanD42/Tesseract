// Models the user imported (Blender .glb and the other formats raylib reads). They appear in the scene list after
// the built-in scenes.
//
// Storage: each import is copied into its own folder under models/ (next to tesseract_calibration.yml), together
// with the .bin / texture / .mtl files it references, and listed in models/library.yml. The original file can be
// moved or deleted afterwards.
//
// Placement: if a glTF file contains a camera (Blender: Export > glTF 2.0, Include > Cameras), that camera becomes
// the viewer's eye at its sitting position, at real scale (1 Blender metre = 1 real metre). Something 40 cm in
// front of the Blender camera then floats 40 cm in front of your eyes, in front of the screen if you sit further
// away than that. Files without a camera are scaled to fit and set just behind the glass.
#pragma once
#include <raylib.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tess {

// ".glb, .gltf, .obj, .iqm, .vox, .m3d" (what raylib can load).
bool IsSupportedModelFile(const std::string& pathUtf8);
inline constexpr const char* kSupportedModelList = ".glb .gltf .obj .iqm .vox .m3d";

// Animation state of the imported scene being shown (kept in AppState, advanced by main.cpp).
struct ModelPlayback {
    int clip = 0;            // animation index
    double frame = 0;        // fractional frame; raylib samples glTF animations at ~60 frames per second
    bool playing = true;
    // Click-and-drag spin (degrees) about the model's center: yaw around the file's up axis, pitch toward the viewer.
    float yaw = 0, pitch = 0;
    float yawVelocity = 0;   // degrees per second, keeps spinning briefly after a flick
};

inline constexpr float kAnimFramesPerSecond = 1000.0f / 17.0f;   // raylib's glTF sampling rate (GLTF_ANIMDELAY)

// One entry in the animation controls. In a glTF file that is one animation (a Blender action), which may move
// bones (played through raylib), objects (keyframed location / rotation / scale, played by ObjectAnimation), or both.
struct AnimClip {
    std::string name;
    int frameCount = 1;      // at kAnimFramesPerSecond
    int skeletal = -1;       // index into LoadedModel::anims, -1 = no bone animation
    int objects = -1;        // glTF animation index with object keyframes, -1 = none
};

struct ObjectAnimation;      // object keyframes read from the glTF file (model_library.cpp)

// GPU data of one imported model.
struct LoadedModel {
    Model model{};
    ModelAnimation* anims = nullptr;   // raylib bone animations (owned)
    int animCount = 0;
    std::vector<AnimClip> clips;
    std::shared_ptr<ObjectAnimation> objectAnim;   // null when the file has no object keyframes
    std::vector<Matrix> meshDelta;     // per mesh: rest pose -> animated pose from object keyframes (empty = rest)
    BoundingBox bounds{};    // in the file's units (metres for glTF), rest pose
    bool hasCamera = false;
    Matrix camera{};         // the file's camera (position + orientation, scale removed), in the file's space
    int posedClip = -1, posedBoneFrame = -1;   // what the vertices currently show
    double posedTime = -1;

    int clipCount() const { return int(clips.size()); }
    // Poses the model at this frame of the clip: bones (CPU skinning) and object keyframes.
    void pose(int clip, double frame);
    // Extra transform for mesh i from object keyframes (identity at rest). Apply before the model's placement.
    Matrix meshTransform(int mesh) const;
    // Rotation about the model's center for the drag spin, in the file's space. Apply after meshTransform.
    Matrix spinTransform(float yawDeg, float pitchDeg) const;
    Vector3 center() const;
};

// Reads dir/library.yml. Does not touch the GPU, so it can run before the window exists.
void InitModelLibrary(const std::filesystem::path& dir);
// Frees all GPU data. Call before CloseWindow().
void ShutdownModelLibrary();

int ImportedModelCount();
const std::string& ImportedModelName(int i);

// Copies the file (plus what it references) into the library and checks that it loads. Needs the window.
// Returns the new model index, or -1 with `error` set. `warning` lists referenced files that were not found.
int ImportModel(const std::string& sourcePathUtf8, std::string& error, std::string& warning);

// Deletes the model's folder and its entry.
bool RemoveImportedModel(int i, std::string& error);

// GPU data for model i, loaded on first use. nullptr if the file no longer loads (see ImportedModelError).
LoadedModel* GetLoadedModel(int i);
const std::string& ImportedModelError(int i);

// Moves the playback forward by dt seconds (looping) and keeps clip/frame in range.
void AdvancePlayback(ModelPlayback& playback, const LoadedModel& model, float dt);

} // namespace tess
