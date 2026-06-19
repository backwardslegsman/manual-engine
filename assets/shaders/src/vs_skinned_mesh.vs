$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_color0, a_indices, a_weight
$output v_texcoord0, v_texcoord1, v_normal, v_tangent, v_worldPos, v_color0

#include <bgfx_shader.sh>

uniform mat4 u_jointPalette[64];

mat4 identityMatrix()
{
    return mat4(
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    );
}

mat4 skinMatrix(vec4 indices, vec4 weights)
{
    vec4 safeIndices = clamp(indices, 0.0, 63.0);
    vec4 safeWeights = clamp(weights, 0.0, 1.0);
    int joint0 = int(safeIndices.x + 0.5);
    int joint1 = int(safeIndices.y + 0.5);
    int joint2 = int(safeIndices.z + 0.5);
    int joint3 = int(safeIndices.w + 0.5);
    float weightSum = safeWeights.x + safeWeights.y + safeWeights.z + safeWeights.w;
    if (weightSum <= 0.00001) {
        return identityMatrix();
    }
    return
        u_jointPalette[joint0] * (safeWeights.x / weightSum) +
        u_jointPalette[joint1] * (safeWeights.y / weightSum) +
        u_jointPalette[joint2] * (safeWeights.z / weightSum) +
        u_jointPalette[joint3] * (safeWeights.w / weightSum);
}

void main()
{
    mat4 skin = skinMatrix(a_indices, a_weight);
    vec4 skinnedPosition = mul(skin, vec4(a_position, 1.0));
    vec3 skinnedNormal = normalize(mul(skin, vec4(a_normal, 0.0)).xyz);
    vec3 skinnedTangent = normalize(mul(skin, vec4(a_tangent.xyz, 0.0)).xyz);

    vec4 worldPosition = mul(u_model[0], skinnedPosition);
    vec3 modelRow0 = vec3(u_model[0][0].x, u_model[0][0].y, u_model[0][0].z);
    vec3 modelRow1 = vec3(u_model[0][1].x, u_model[0][1].y, u_model[0][1].z);
    vec3 modelRow2 = vec3(u_model[0][2].x, u_model[0][2].y, u_model[0][2].z);
    vec3 normalRow0 = cross(modelRow1, modelRow2);
    vec3 normalRow1 = cross(modelRow2, modelRow0);
    vec3 normalRow2 = cross(modelRow0, modelRow1);
    mat3 normalMatrix = mat3(
        normalRow0.x, normalRow0.y, normalRow0.z,
        normalRow1.x, normalRow1.y, normalRow1.z,
        normalRow2.x, normalRow2.y, normalRow2.z
    );
    gl_Position = mul(u_modelViewProj, skinnedPosition);
    v_texcoord0 = a_texcoord0;
    v_texcoord1 = a_texcoord1;
    v_normal = normalize(mul(normalMatrix, skinnedNormal));
    v_tangent = vec4(normalize(mul(normalMatrix, skinnedTangent)), a_tangent.w);
    v_worldPos = worldPosition.xyz;
    v_color0 = a_color0;
}
