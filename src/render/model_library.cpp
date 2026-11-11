#include "render/model_library.h"

#include <raymath.h>
#include <rlgl.h>
#include <opencv2/core.hpp>
#include <opencv2/core/persistence.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

// raylib compiles cgltf (rmodels.c defines CGLTF_IMPLEMENTATION); we only need the declarations to read the camera.
#include <external/cgltf.h>

namespace tess {
namespace fs = std::filesystem;
namespace {

constexpr const char* kLibraryFile = "library.yml";

struct Entry {
    std::string name;    // shown in the scene selector (UTF-8)
    std::string file;    // relative to the library folder, e.g. "chair/chair.glb"
    ImportedSceneInfo info;
    std::unique_ptr<LoadedModel> gpu;
    bool failed = false;
    std::string error;
};

struct Library {
    fs::path dir;
    std::vector<Entry> entries;
} g;

std::string Lower(std::string s) {
    for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::string ExtensionOf(const fs::path& p) { return Lower(p.extension().u8string()); }

// Folder / file name that raylib's narrow-character file functions can open on any Windows code page.
std::string AsciiName(const std::string& utf8) {
    std::string out;
    for (unsigned char ch : utf8) {
        const bool ok = std::isalnum(ch) || ch == '-' || ch == '_';
        if (ch < 128 && ok) out.push_back(char(ch));
        else if (!out.empty() && out.back() != '_') out.push_back('_');
        if (out.size() >= 40) break;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "model" : out;
}

bool Save() {
    std::error_code ec;
    fs::create_directories(g.dir, ec);
    cv::FileStorage store;
    try {
        if (!store.open((g.dir / kLibraryFile).u8string(), cv::FileStorage::WRITE)) return false;
        store << "models" << "[";
        for (const Entry& e : g.entries) {
            store << "{" << "name" << e.name << "file" << e.file << "description" << e.info.description
                  << "allow_rotation" << int(e.info.allowRotation) << "backdrop" << int(e.info.backdrop)
                  << "autoplay" << int(e.info.autoplay) << "}";
        }
        store << "]";
    } catch (const cv::Exception&) {
        return false;
    }
    return true;
}

// Copies srcDir/rel to dstDir/rel. Paths that leave the model's folder (absolute, "..") cannot be kept relative,
// so they are reported instead.
void CopyDependency(const fs::path& srcDir, const fs::path& dstDir, const std::string& relUtf8,
                    std::vector<std::string>& missing) {
    if (relUtf8.empty() || relUtf8.rfind("data:", 0) == 0 || relUtf8.find("://") != std::string::npos) return;
    const fs::path rel = fs::u8path(relUtf8).lexically_normal();
    bool escapes = rel.is_absolute() || rel.has_root_name();
    for (const fs::path& part : rel) if (part == "..") escapes = true;
    std::error_code ec;
    const fs::path from = srcDir / rel;
    if (escapes || !fs::is_regular_file(from, ec)) { missing.push_back(relUtf8); return; }
    const fs::path to = dstDir / rel;
    fs::create_directories(to.parent_path(), ec);
    if (!fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec)) missing.push_back(relUtf8);
}

// .gltf (text) keeps its geometry in .bin files and its textures in image files next to it.
void CopyGltfDependencies(const fs::path& copiedGltf, const fs::path& srcDir, const fs::path& dstDir,
                          std::vector<std::string>& missing) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, copiedGltf.u8string().c_str(), &data) != cgltf_result_success) return;
    auto copyUri = [&](const char* uri) {
        if (!uri) return;
        std::string s = uri;
        cgltf_decode_uri(&s[0]);
        s.resize(std::strlen(s.c_str()));
        CopyDependency(srcDir, dstDir, s, missing);
    };
    for (cgltf_size i = 0; i < data->buffers_count; ++i) copyUri(data->buffers[i].uri);
    for (cgltf_size i = 0; i < data->images_count; ++i) copyUri(data->images[i].uri);
    cgltf_free(data);
}

std::vector<std::string> Tokens(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    for (std::string t; in >> t;) out.push_back(t);
    return out;
}

// .obj references .mtl material files ("mtllib"), which reference textures ("map_Kd" etc.).
void CopyObjDependencies(const fs::path& obj, const fs::path& srcDir, const fs::path& dstDir,
                         std::vector<std::string>& missing) {
    std::ifstream in(obj);
    for (std::string line; std::getline(in, line);) {
        const std::vector<std::string> t = Tokens(line);
        if (t.size() < 2 || t[0] != "mtllib") continue;
        for (size_t i = 1; i < t.size(); ++i) {
            CopyDependency(srcDir, dstDir, t[i], missing);
            std::ifstream mtl(srcDir / fs::u8path(t[i]));
            for (std::string ml; std::getline(mtl, ml);) {
                const std::vector<std::string> mt = Tokens(ml);
                if (mt.size() < 2) continue;
                const std::string key = Lower(mt[0]);
                if (key.rfind("map_", 0) == 0 || key == "bump" || key == "disp" || key == "decal" || key == "refl" ||
                    key == "norm")
                    CopyDependency(srcDir, dstDir, mt.back(), missing);   // options (-s 1 1 1 ...) come first
            }
        }
    }
}

// The first camera in a glTF file, with any scale on the camera object removed.
bool ReadGltfCamera(const fs::path& file, Matrix& out) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, file.u8string().c_str(), &data) != cgltf_result_success) return false;
    bool found = false;
    for (cgltf_size i = 0; i < data->nodes_count && !found; ++i) {
        if (!data->nodes[i].camera) continue;
        float m[16];   // column-major
        cgltf_node_transform_world(&data->nodes[i], m);
        const Vector3 x = Vector3Normalize({m[0], m[1], m[2]});
        const Vector3 y = Vector3Normalize({m[4], m[5], m[6]});
        const Vector3 z = Vector3Normalize({m[8], m[9], m[10]});
        out = Matrix{x.x, y.x, z.x, m[12],
                     x.y, y.y, z.y, m[13],
                     x.z, y.z, z.z, m[14],
                     0,   0,   0,   1};
        found = true;
    }
    cgltf_free(data);
    return found;
}

} // namespace

// ---------------------------------------------------------------- object keyframes ------
// raylib bakes every node's rest world transform into its meshes and ignores node animation. We read the node
// hierarchy and the translation / rotation / scale channels ourselves, and give each mesh the transform
// "rest world -> animated world" of its node.
struct ObjectAnimation {
    struct Node {
        int parent = -1;
        Vector3 t{0, 0, 0};
        Quaternion r{0, 0, 0, 1};
        Vector3 s{1, 1, 1};
        Matrix restWorldInv = MatrixIdentity();
    };
    enum Path { kTranslation, kRotation, kScale };
    enum Interp { kLinear, kStep, kCubic };
    struct Channel {
        int node = 0;
        Path path = kTranslation;
        Interp interp = kLinear;
        int comps = 3;
        std::vector<float> times, values;   // cubic: per key in-tangent, value, out-tangent
    };
    std::vector<Node> nodes;
    std::vector<int> meshNode;                    // per raylib mesh: its node, -1 for skinned meshes (bones move those)
    std::vector<std::vector<Channel>> channels;   // per glTF animation
    std::vector<float> duration;                  // per glTF animation, seconds

    static Matrix Local(Vector3 t, Quaternion r, Vector3 s) {
        return MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), QuaternionToMatrix(r)),
                              MatrixTranslate(t.x, t.y, t.z));
    }

    // World matrices of all nodes for the given local transforms.
    std::vector<Matrix> World(const std::vector<Vector3>& T, const std::vector<Quaternion>& R,
                              const std::vector<Vector3>& S) const {
        const int n = int(nodes.size());
        std::vector<Matrix> w(n);
        std::vector<char> done(n, 0);
        for (int i = 0; i < n; ++i) {
            // Walk up to the first finished ancestor, then fill in downward.
            std::vector<int> chain;
            for (int k = i; k >= 0 && !done[k] && chain.size() <= size_t(n); k = nodes[k].parent) chain.push_back(k);
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                const int k = *it;
                const Matrix local = Local(T[k], R[k], S[k]);
                w[k] = nodes[k].parent >= 0 ? MatrixMultiply(local, w[nodes[k].parent]) : local;
                done[k] = 1;
            }
        }
        return w;
    }

    static void Sample(const Channel& ch, float t, float* out) {
        const int n = int(ch.times.size()), c = ch.comps;
        auto key = [&](int k, int part) { return &ch.values[size_t((ch.interp == kCubic ? k * 3 + part : k) * c)]; };
        auto copy = [&](const float* v) { for (int i = 0; i < c; ++i) out[i] = v[i]; };
        if (n == 1 || t <= ch.times[0]) { copy(key(0, 1)); return; }
        if (t >= ch.times[n - 1]) { copy(key(n - 1, 1)); return; }
        const int k = int(std::upper_bound(ch.times.begin(), ch.times.end(), t) - ch.times.begin()) - 1;
        const float t0 = ch.times[k], dt = std::max(ch.times[k + 1] - t0, 1e-6f), u = (t - t0) / dt;
        if (ch.interp == kStep) { copy(key(k, 1)); return; }
        if (ch.interp == kLinear) {
            const float *a = key(k, 1), *b = key(k + 1, 1);
            if (c == 4) {
                const Quaternion q = QuaternionSlerp({a[0], a[1], a[2], a[3]}, {b[0], b[1], b[2], b[3]}, u);
                out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
            } else {
                for (int i = 0; i < c; ++i) out[i] = a[i] + (b[i] - a[i]) * u;
            }
            return;
        }
        // Cubic spline (Blender's Bezier keyframes): Hermite between the two keys.
        const float u2 = u * u, u3 = u2 * u;
        const float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
        const float *p0 = key(k, 1), *m0 = key(k, 2), *p1 = key(k + 1, 1), *m1 = key(k + 1, 0);
        for (int i = 0; i < c; ++i) out[i] = h00 * p0[i] + h10 * dt * m0[i] + h01 * p1[i] + h11 * dt * m1[i];
        if (c == 4) {
            const Quaternion q = QuaternionNormalize({out[0], out[1], out[2], out[3]});
            out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
        }
    }

    // Per-mesh "rest -> animated" transforms for glTF animation `anim` at `seconds`.
    void Evaluate(int anim, float seconds, std::vector<Matrix>& meshDelta) const {
        const int n = int(nodes.size());
        std::vector<Vector3> T(n), S(n);
        std::vector<Quaternion> R(n);
        for (int i = 0; i < n; ++i) { T[i] = nodes[i].t; R[i] = nodes[i].r; S[i] = nodes[i].s; }
        float v[4];
        for (const Channel& ch : channels[anim]) {
            Sample(ch, seconds, v);
            if (ch.path == kTranslation) T[ch.node] = {v[0], v[1], v[2]};
            else if (ch.path == kRotation) R[ch.node] = {v[0], v[1], v[2], v[3]};
            else S[ch.node] = {v[0], v[1], v[2]};
        }
        const std::vector<Matrix> w = World(T, R, S);
        meshDelta.resize(meshNode.size());
        for (size_t m = 0; m < meshNode.size(); ++m) {
            const int node = meshNode[m];
            meshDelta[m] = node < 0 ? MatrixIdentity() : MatrixMultiply(nodes[node].restWorldInv, w[node]);
        }
    }
};

namespace {

// Reads node hierarchy + animations. Builds the clip list for glTF files (one clip per glTF animation that moves
// something: bones raylib can play and/or objects that carry meshes).
void ReadGltfAnimation(const fs::path& file, LoadedModel& lm) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    const std::string path = file.u8string();
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) return;
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) { cgltf_free(data); return; }

    auto oa = std::make_shared<ObjectAnimation>();
    const int nodeCount = int(data->nodes_count);
    oa->nodes.resize(nodeCount);
    for (int i = 0; i < nodeCount; ++i) {
        const cgltf_node& n = data->nodes[i];
        ObjectAnimation::Node& o = oa->nodes[i];
        o.parent = n.parent ? int(n.parent - data->nodes) : -1;
        if (n.has_matrix) {
            const float* f = n.matrix;
            const Matrix m{f[0], f[4], f[8], f[12], f[1], f[5], f[9], f[13], f[2], f[6], f[10], f[14], f[3], f[7], f[11], f[15]};
            MatrixDecompose(m, &o.t, &o.r, &o.s);
        } else {
            if (n.has_translation) o.t = {n.translation[0], n.translation[1], n.translation[2]};
            if (n.has_rotation) o.r = {n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]};
            if (n.has_scale) o.s = {n.scale[0], n.scale[1], n.scale[2]};
        }
    }
    {
        std::vector<Vector3> T(nodeCount), S(nodeCount);
        std::vector<Quaternion> R(nodeCount);
        for (int i = 0; i < nodeCount; ++i) { T[i] = oa->nodes[i].t; R[i] = oa->nodes[i].r; S[i] = oa->nodes[i].s; }
        const std::vector<Matrix> rest = oa->World(T, R, S);
        for (int i = 0; i < nodeCount; ++i) oa->nodes[i].restWorldInv = MatrixInvert(rest[i]);
    }
    // Same order as raylib's LoadGLTF: nodes in file order, one mesh per triangle primitive.
    std::vector<char> carriesMesh(nodeCount, 0);   // node is, or is an ancestor of, a node with a rigid mesh
    for (int i = 0; i < nodeCount; ++i) {
        const cgltf_node& n = data->nodes[i];
        if (!n.mesh) continue;
        for (cgltf_size p = 0; p < n.mesh->primitives_count; ++p) {
            if (n.mesh->primitives[p].type != cgltf_primitive_type_triangles) continue;
            oa->meshNode.push_back(n.skin ? -1 : i);
        }
        if (!n.skin)
            for (int k = i, guard = 0; k >= 0 && guard <= nodeCount; k = oa->nodes[k].parent, ++guard) carriesMesh[k] = 1;
    }
    const bool meshesMatch = int(oa->meshNode.size()) == lm.model.meshCount;

    bool anyObjects = false;
    for (cgltf_size a = 0; a < data->animations_count; ++a) {
        const cgltf_animation& anim = data->animations[a];
        std::vector<ObjectAnimation::Channel> chans;
        float duration = 0;
        bool movesMesh = false;
        for (cgltf_size c = 0; c < anim.channels_count && meshesMatch; ++c) {
            const cgltf_animation_channel& src = anim.channels[c];
            if (!src.target_node || !src.sampler || !src.sampler->input || !src.sampler->output) continue;
            ObjectAnimation::Channel ch;
            ch.node = int(src.target_node - data->nodes);
            switch (src.target_path) {
                case cgltf_animation_path_type_translation: ch.path = ObjectAnimation::kTranslation; ch.comps = 3; break;
                case cgltf_animation_path_type_rotation: ch.path = ObjectAnimation::kRotation; ch.comps = 4; break;
                case cgltf_animation_path_type_scale: ch.path = ObjectAnimation::kScale; ch.comps = 3; break;
                default: continue;   // morph target weights are not supported
            }
            ch.interp = src.sampler->interpolation == cgltf_interpolation_type_step ? ObjectAnimation::kStep
                      : src.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? ObjectAnimation::kCubic
                      : ObjectAnimation::kLinear;
            const cgltf_accessor* in = src.sampler->input;
            const cgltf_accessor* out = src.sampler->output;
            const size_t keys = in->count, values = out->count;
            if (keys == 0 || values != keys * (ch.interp == ObjectAnimation::kCubic ? 3 : 1)) continue;
            ch.times.resize(keys);
            ch.values.resize(values * ch.comps);
            bool ok = true;
            for (size_t k = 0; k < keys && ok; ++k) ok = cgltf_accessor_read_float(in, k, &ch.times[k], 1);
            for (size_t k = 0; k < values && ok; ++k) ok = cgltf_accessor_read_float(out, k, &ch.values[k * ch.comps], ch.comps);
            if (!ok) continue;
            duration = std::max(duration, ch.times.back());
            movesMesh = movesMesh || carriesMesh[ch.node];
            chans.push_back(std::move(ch));
        }
        oa->channels.push_back(movesMesh ? std::move(chans) : std::vector<ObjectAnimation::Channel>{});
        oa->duration.push_back(duration);
        anyObjects = anyObjects || movesMesh;

        AnimClip clip;
        clip.name = anim.name && anim.name[0] ? anim.name : "Animation " + std::to_string(a + 1);
        if (lm.anims && int(a) < lm.animCount && lm.anims[a].frameCount > 0 && IsModelAnimationValid(lm.model, lm.anims[a]))
            clip.skeletal = int(a);
        if (movesMesh) clip.objects = int(a);
        if (clip.skeletal < 0 && clip.objects < 0) continue;
        clip.frameCount = std::max(clip.skeletal >= 0 ? lm.anims[a].frameCount : 1,
                                   int(duration * kAnimFramesPerSecond) + 1);
        lm.clips.push_back(std::move(clip));
    }
    if (anyObjects) lm.objectAnim = std::move(oa);
    cgltf_free(data);
}

void UnloadGpu(LoadedModel& lm) {
    // The materials may point at the scene shader, which raylib would otherwise unload with the model.
    for (int i = 0; i < lm.model.materialCount; ++i)
        lm.model.materials[i].shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
    if (lm.anims) UnloadModelAnimations(lm.anims, lm.animCount);
    UnloadModel(lm.model);
    lm = LoadedModel{};
}

std::unique_ptr<LoadedModel> LoadGpu(const fs::path& file, std::string& error) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) { error = "Missing file: " + file.u8string(); return nullptr; }
    auto lm = std::make_unique<LoadedModel>();
    lm->model = LoadModel(file.u8string().c_str());
    if (lm->model.meshCount == 0 || lm->model.meshes == nullptr) {
        UnloadModel(lm->model);
        error = "Could not read any meshes from " + file.filename().u8string();
        return nullptr;
    }
    lm->bounds = GetModelBoundingBox(lm->model);
    const std::string ext = ExtensionOf(file);
    const bool gltf = ext == ".glb" || ext == ".gltf";
    if (gltf || ext == ".iqm" || ext == ".m3d") {
        // Bone animations (raylib). For glTF their indices match the file's animations.
        lm->anims = LoadModelAnimations(file.u8string().c_str(), &lm->animCount);
        if (!lm->anims) lm->animCount = 0;
    }
    if (gltf) {
        lm->hasCamera = ReadGltfCamera(file, lm->camera);
        ReadGltfAnimation(file, *lm);   // clips: bones and/or object keyframes
    } else {
        for (int i = 0; i < lm->animCount; ++i) {
            if (lm->anims[i].frameCount <= 0 || !IsModelAnimationValid(lm->model, lm->anims[i])) continue;
            AnimClip clip;
            clip.name = lm->anims[i].name[0] ? lm->anims[i].name : "Animation " + std::to_string(i + 1);
            clip.skeletal = i;
            clip.frameCount = lm->anims[i].frameCount;
            lm->clips.push_back(std::move(clip));
        }
    }
    return lm;
}

} // namespace

bool IsSupportedModelFile(const std::string& pathUtf8) {
    const std::string ext = ExtensionOf(fs::u8path(pathUtf8));
    return ext == ".glb" || ext == ".gltf" || ext == ".obj" || ext == ".iqm" || ext == ".vox" || ext == ".m3d";
}

// CPU skinning for one bone-animation frame (below).
void SkinMeshes(Model& model, const ModelAnimation& a, int frame);

Matrix LoadedModel::meshTransform(int mesh) const {
    return mesh >= 0 && mesh < int(meshDelta.size()) ? meshDelta[mesh] : MatrixIdentity();
}

Vector3 LoadedModel::center() const { return Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f); }

Matrix LoadedModel::spinTransform(float yawDeg, float pitchDeg) const {
    if (yawDeg == 0 && pitchDeg == 0) return MatrixIdentity();
    const Vector3 c = center();
    // Pitch axis: the viewer's right, kept horizontal. With a camera that is the camera's X axis; otherwise the
    // model is placed unrotated and the viewer's right is +X.
    Vector3 right{1, 0, 0};
    if (hasCamera) {
        const Vector3 r{camera.m0, 0, camera.m2};
        if (Vector3Length(r) > 1e-3f) right = Vector3Normalize(r);
    }
    const Matrix rot = MatrixMultiply(MatrixRotateY(yawDeg * DEG2RAD), MatrixRotate(right, pitchDeg * DEG2RAD));
    return MatrixMultiply(MatrixMultiply(MatrixTranslate(-c.x, -c.y, -c.z), rot), MatrixTranslate(c.x, c.y, c.z));
}

void LoadedModel::pose(int clipIndex, double frame) {
    if (clipIndex < 0 || clipIndex >= clipCount()) return;
    const AnimClip& clip = clips[clipIndex];
    const bool newClip = clipIndex != posedClip;
    posedClip = clipIndex;

    // Object keyframes.
    if (clip.objects >= 0 && objectAnim) {
        const double seconds = std::max(0.0, frame) / kAnimFramesPerSecond;
        if (newClip || seconds != posedTime) objectAnim->Evaluate(clip.objects, float(seconds), meshDelta);
        posedTime = seconds;
    } else {
        meshDelta.clear();
        posedTime = -1;
    }

    // Bones. The clip may run longer than its bone animation: hold the last pose.
    if (clip.skeletal < 0) return;
    const ModelAnimation& a = anims[clip.skeletal];
    const int boneFrame = std::clamp(int(frame), 0, a.frameCount - 1);
    if (!newClip && boneFrame == posedBoneFrame) return;
    posedBoneFrame = boneFrame;
    SkinMeshes(model, a, boneFrame);
}

void SkinMeshes(Model& model, const ModelAnimation& a, int frame) {
    UpdateModelAnimationBones(model, a, frame);
    // CPU skinning of positions only (the scene shader rebuilds normals itself). raylib's UpdateModelAnimation
    // does the same but touches every mesh, including ones without bones.
    for (int m = 0; m < model.meshCount; ++m) {
        Mesh& mesh = model.meshes[m];
        if (!mesh.boneMatrices || !mesh.boneWeights || !mesh.boneIds || !mesh.animVertices || !mesh.vertices) continue;
        for (int v = 0; v < mesh.vertexCount; ++v) {
            const Vector3 p{mesh.vertices[3 * v], mesh.vertices[3 * v + 1], mesh.vertices[3 * v + 2]};
            Vector3 sum{0, 0, 0};
            float total = 0;
            for (int j = 0; j < 4; ++j) {
                const float w = mesh.boneWeights[4 * v + j];
                const int bone = mesh.boneIds[4 * v + j];
                if (w == 0.0f || bone >= mesh.boneCount) continue;
                sum = Vector3Add(sum, Vector3Scale(Vector3Transform(p, mesh.boneMatrices[bone]), w));
                total += w;
            }
            if (total <= 0.0f) sum = p;
            mesh.animVertices[3 * v] = sum.x;
            mesh.animVertices[3 * v + 1] = sum.y;
            mesh.animVertices[3 * v + 2] = sum.z;
        }
        rlUpdateVertexBuffer(mesh.vboId[0], mesh.animVertices, mesh.vertexCount * 3 * sizeof(float), 0);
    }
}

void InitModelLibrary(const fs::path& dir) {
    g.dir = dir;
    g.entries.clear();
    std::error_code ec;
    if (!fs::exists(dir / kLibraryFile, ec)) return;
    cv::FileStorage store;
    try {
        if (!store.open((dir / kLibraryFile).u8string(), cv::FileStorage::READ)) return;
        const cv::FileNode list = store["models"];
        if (!list.isSeq()) return;
        for (const cv::FileNode& n : list) {
            Entry e;
            e.name = std::string(n["name"]);
            e.file = std::string(n["file"]);
            if (e.file.empty()) continue;
            if (e.name.empty()) e.name = fs::u8path(e.file).stem().u8string();
            e.info.description = std::string(n["description"]);
            auto flag = [&](const char* key, bool& v) { const cv::FileNode f = n[key]; if (f.isInt()) v = int(f) != 0; };
            flag("allow_rotation", e.info.allowRotation);
            flag("backdrop", e.info.backdrop);
            flag("autoplay", e.info.autoplay);
            g.entries.push_back(std::move(e));
        }
    } catch (const cv::Exception&) {
    }
}

void ShutdownModelLibrary() {
    for (Entry& e : g.entries)
        if (e.gpu) { UnloadGpu(*e.gpu); e.gpu.reset(); }
}

int ImportedModelCount() { return int(g.entries.size()); }

const std::string& ImportedModelName(int i) {
    static const std::string none;
    return i >= 0 && i < ImportedModelCount() ? g.entries[i].name : none;
}

const ImportedSceneInfo& ImportedModelInfo(int i) {
    static const ImportedSceneInfo none;
    return i >= 0 && i < ImportedModelCount() ? g.entries[i].info : none;
}

bool SetImportedModelInfo(int i, const ImportedSceneInfo& info) {
    if (i < 0 || i >= ImportedModelCount()) return false;
    g.entries[i].info = info;
    return Save();
}

const std::string& ImportedModelError(int i) {
    static const std::string none;
    return i >= 0 && i < ImportedModelCount() ? g.entries[i].error : none;
}

int ImportModel(const std::string& sourcePathUtf8, std::string& error, std::string& warning) {
    error.clear();
    warning.clear();
    const fs::path from = fs::u8path(sourcePathUtf8);
    std::error_code ec;
    const std::string ext = ExtensionOf(from);
    if (ext == ".blend") {
        error = "Blender files can't be read directly: in Blender use File > Export > glTF 2.0 (.glb), Include > Cameras";
        return -1;
    }
    if (!IsSupportedModelFile(sourcePathUtf8)) {
        error = std::string("Unsupported file type (use ") + kSupportedModelList + ")";
        return -1;
    }
    if (!fs::is_regular_file(from, ec)) { error = "File not found: " + sourcePathUtf8; return -1; }

    // Its own folder: models/<name>[_2...]/<name>.<ext>
    const std::string base = AsciiName(from.stem().u8string());
    std::string folder = base;
    for (int n = 2; fs::exists(g.dir / folder, ec); ++n) folder = base + "_" + std::to_string(n);
    const fs::path dstDir = g.dir / folder;
    const fs::path mainFile = dstDir / (base + ext);
    if (!fs::create_directories(dstDir, ec) || !fs::copy_file(from, mainFile, ec)) {
        error = "Could not copy the model into " + dstDir.u8string();
        fs::remove_all(dstDir, ec);
        return -1;
    }
    std::vector<std::string> missing;
    if (ext == ".gltf") CopyGltfDependencies(mainFile, from.parent_path(), dstDir, missing);
    if (ext == ".obj") CopyObjDependencies(from, from.parent_path(), dstDir, missing);

    std::unique_ptr<LoadedModel> gpu = LoadGpu(mainFile, error);
    if (!gpu) { fs::remove_all(dstDir, ec); return -1; }

    Entry e;
    e.name = from.stem().u8string();
    for (int n = 2;; ++n) {
        const bool taken = std::any_of(g.entries.begin(), g.entries.end(), [&](const Entry& o) { return o.name == e.name; });
        if (!taken) break;
        e.name = from.stem().u8string() + " (" + std::to_string(n) + ")";
    }
    e.file = (fs::path(folder) / (base + ext)).generic_u8string();
    e.gpu = std::move(gpu);
    g.entries.push_back(std::move(e));
    if (!Save()) warning = "Imported, but could not write " + (g.dir / kLibraryFile).u8string() + ". ";
    if (!missing.empty()) {
        warning += std::to_string(missing.size()) + " referenced file(s) not found or outside the model's folder: " + missing[0];
        if (missing.size() > 1) warning += ", ...";
    }
    return ImportedModelCount() - 1;
}

bool RemoveImportedModel(int i, std::string& error) {
    if (i < 0 || i >= ImportedModelCount()) { error = "No such model"; return false; }
    Entry& e = g.entries[i];
    if (e.gpu) UnloadGpu(*e.gpu);
    const fs::path rel = fs::u8path(e.file).lexically_normal();
    std::error_code ec;
    // Only ever delete a folder directly inside the library.
    if (std::distance(rel.begin(), rel.end()) >= 2 && *rel.begin() != ".." && !rel.is_absolute())
        fs::remove_all(g.dir / *rel.begin(), ec);
    g.entries.erase(g.entries.begin() + i);
    if (!Save()) { error = "Could not write " + (g.dir / kLibraryFile).u8string(); return false; }
    return true;
}

LoadedModel* GetLoadedModel(int i) {
    if (i < 0 || i >= ImportedModelCount()) return nullptr;
    Entry& e = g.entries[i];
    if (!e.gpu && !e.failed) {
        e.gpu = LoadGpu(g.dir / fs::u8path(e.file), e.error);
        e.failed = !e.gpu;
    }
    return e.gpu.get();
}

void AdvancePlayback(ModelPlayback& p, const LoadedModel& m, float dt) {
    if (m.clipCount() <= 0) { p.clip = 0; p.frame = 0; return; }
    p.clip = std::clamp(p.clip, 0, m.clipCount() - 1);
    const double count = std::max(1, m.clips[p.clip].frameCount);
    if (p.playing) p.frame += dt * kAnimFramesPerSecond;
    p.frame = std::fmod(p.frame, count);
    if (p.frame < 0) p.frame += count;
}

} // namespace tess
