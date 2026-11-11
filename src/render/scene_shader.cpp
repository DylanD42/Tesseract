#include "render/scene_shader.h"

#include <raymath.h>
#include <rlgl.h>
#include <iostream>

namespace tess {
namespace {

const char* kSceneVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;   // identity for the scenes' immediate-mode geometry; set by raylib for imported models
out vec3 fragPos;
out vec2 fragTexCoord;
out vec4 fragColor;
void main() {
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* kSceneFS = R"(#version 330
in vec3 fragPos;
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse; // material base color (white for immediate-mode geometry)
uniform vec3 viewPos;
uniform vec3 lightDir;   // direction the light travels
uniform vec3 fogColor;
uniform float fogNear;
uniform float fogFar;
uniform float fogMax;
uniform float lit;       // 1 = shaded surfaces, 0 = lines / glow / shadows (fog only)
uniform float shine;     // specular amount: 1 = glossy objects, 0 = matte ground, rugs, grass
out vec4 finalColor;
void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec3 col = fragColor.rgb * texel.rgb * colDiffuse.rgb;
    vec3 toEye = viewPos - fragPos;
    if (lit > 0.5) {
        vec3 n = cross(dFdx(fragPos), dFdy(fragPos));
        float len = length(n);
        if (len > 1e-12) {
            n /= len;
            vec3 V = normalize(toEye);
            if (dot(n, V) < 0.0) n = -n;
            vec3 L = normalize(-lightDir);
            float diff = max(dot(n, L), 0.0);
            float hemi = 0.5 + 0.5 * n.y;
            vec3 H = normalize(L + V);
            float spec = pow(max(dot(n, H), 0.0), 40.0) * 0.25 * shine;
            col = col * (0.28 + 0.20 * hemi + 0.62 * diff) + vec3(spec);
        }
    }
    float fog = clamp((length(toEye) - fogNear) / max(fogFar - fogNear, 1e-3), 0.0, 1.0);
    col = mix(col, fogColor, fog * fogMax);
    finalColor = vec4(col, fragColor.a * texel.a * colDiffuse.a);
}
)";

} // namespace

void SceneShader::load() {
    sh = LoadShaderFromMemory(kSceneVS, kSceneFS);
    ok = IsShaderValid(sh) && sh.id != rlGetShaderIdDefault();
    if (!ok) { std::cerr << "Scene shader failed; falling back to unlit rendering\n"; return; }
    viewPos  = GetShaderLocation(sh, "viewPos");
    lightDir = GetShaderLocation(sh, "lightDir");
    fogColor = GetShaderLocation(sh, "fogColor");
    fogNear  = GetShaderLocation(sh, "fogNear");
    fogFar   = GetShaderLocation(sh, "fogFar");
    fogMax   = GetShaderLocation(sh, "fogMax");
    lit      = GetShaderLocation(sh, "lit");
    shine    = GetShaderLocation(sh, "shine");
    resetModelMatrix();
}

void SceneShader::unload() {
    if (ok) UnloadShader(sh);
    ok = false;
}

void SceneShader::resetModelMatrix() const {
    if (!ok || sh.locs[SHADER_LOC_MATRIX_MODEL] < 0) return;
    rlDrawRenderBatchActive();
    SetShaderValueMatrix(sh, sh.locs[SHADER_LOC_MATRIX_MODEL], MatrixIdentity());
}

void SceneShader::setMaterial(bool on, float shineAmount) const {
    if (!ok) return;
    rlDrawRenderBatchActive();
    const float v = on ? 1.0f : 0.0f;
    SetShaderValue(sh, lit, &v, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, shine, &shineAmount, SHADER_UNIFORM_FLOAT);
}

} // namespace tess
