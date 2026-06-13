$input v_texcoord0, v_normal, v_tangent, v_worldPos

#include <bgfx_shader.sh>

SAMPLER2D(s_baseColor, 0);
SAMPLER2D(s_normalMap, 1);
SAMPLER2D(s_metallicMap, 2);
SAMPLER2D(s_roughnessMap, 3);

uniform vec4 u_baseColorFactor;
uniform vec4 u_materialParams;
uniform vec4 u_textureFlags;
uniform vec4 u_lightDirection;
uniform vec4 u_sunColorIntensity;
uniform vec4 u_cameraPosition;
uniform vec4 u_fogColor;
uniform vec4 u_fogParams;

void main()
{
    vec4 baseColor = texture2D(s_baseColor, v_texcoord0) * u_baseColorFactor;

    float metallic = mix(u_materialParams.x, texture2D(s_metallicMap, v_texcoord0).r, u_materialParams.z);
    float roughness = mix(u_materialParams.y, texture2D(s_roughnessMap, v_texcoord0).r, u_materialParams.w);
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, 0.04, 1.0);

    vec3 normal = normalize(v_normal);
    if (u_textureFlags.x > 0.5) {
        vec3 tangent = normalize(v_tangent);
        vec3 bitangent = normalize(cross(normal, tangent));
        mat3 tangentToWorld = mat3(tangent, bitangent, normal);
        vec3 sampledNormal = texture2D(s_normalMap, v_texcoord0).xyz * 2.0 - 1.0;
        normal = normalize(mul(tangentToWorld, sampledNormal));
    }

    vec3 lightDir = normalize(-u_lightDirection.xyz);
    vec3 viewDir = vec3(0.0, 0.0, 1.0);
    vec3 halfDir = normalize(lightDir + viewDir);

    float ndotl = max(dot(normal, lightDir), 0.0);
    float ndoth = max(dot(normal, halfDir), 0.0);
    float specPower = mix(64.0, 8.0, roughness);
    float specular = pow(ndoth, specPower) * (1.0 - roughness);

    vec3 sunLight = u_sunColorIntensity.rgb * u_sunColorIntensity.a;
    vec3 diffuse = baseColor.rgb * ndotl * (1.0 - metallic) * sunLight;
    vec3 metalSpecular = baseColor.rgb * specular * metallic * sunLight;
    vec3 dielectricSpecular = vec3(0.04, 0.04, 0.04) * specular * (1.0 - metallic) * sunLight;
    vec3 ambient = baseColor.rgb * 0.08;
    vec3 litColor = ambient + diffuse + metalSpecular + dielectricSpecular;

    float fogDistance = distance(u_cameraPosition.xyz, v_worldPos);
    float fogFactor = clamp(1.0 - exp(-fogDistance * max(u_fogParams.x, 0.0)), 0.0, 1.0) * u_fogParams.y;
    vec3 finalColor = mix(litColor, u_fogColor.rgb, fogFactor);

    gl_FragColor = vec4(finalColor, baseColor.a);
}
