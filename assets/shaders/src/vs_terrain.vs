$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_color0
$output v_texcoord0, v_texcoord1, v_normal, v_tangent, v_worldPos, v_color0

#include <bgfx_shader.sh>

void main()
{
    vec4 worldPosition = mul(u_model[0], vec4(a_position, 1.0));
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
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_texcoord0 = a_texcoord0;
    v_texcoord1 = a_texcoord1;
    v_normal = normalize(mul(normalMatrix, a_normal));
    v_tangent = vec4(normalize(mul(normalMatrix, a_tangent.xyz)), a_tangent.w);
    v_worldPos = worldPosition.xyz;
    v_color0 = a_color0;
}
