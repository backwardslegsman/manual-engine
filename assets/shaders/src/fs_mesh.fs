$input v_texcoord0, v_texcoord1, v_normal, v_tangent, v_worldPos, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_baseColor, 0);
SAMPLER2D(s_normalMap, 1);
SAMPLER2D(s_metallicMap, 2);
SAMPLER2D(s_roughnessMap, 3);
SAMPLER2D(s_metallicRoughnessMap, 4);
SAMPLER2D(s_occlusionMap, 5);
SAMPLER2D(s_emissiveMap, 6);

uniform vec4 u_baseColorFactor;
uniform vec4 u_emissiveFactor;
uniform vec4 u_materialParams;
uniform vec4 u_textureFlags;
uniform vec4 u_packedTextureChannels;
uniform vec4 u_alphaParams;
uniform vec4 u_lightDirection;
uniform vec4 u_sunColorIntensity;
uniform vec4 u_cameraPosition;
uniform vec4 u_pbrParams;
uniform vec4 u_environmentParams;
uniform vec4 u_forwardLightPositionRange[16];
uniform vec4 u_forwardLightDirectionType[16];
uniform vec4 u_forwardLightColorIntensity[16];
uniform vec4 u_forwardLightSpotParams[16];
uniform vec4 u_fogColor;
uniform vec4 u_fogParams;

static const float PI = 3.14159265359;

float selectChannel(vec4 value, float channel)
{
    if (channel < 0.5) {
        return value.r;
    }
    if (channel < 1.5) {
        return value.g;
    }
    if (channel < 2.5) {
        return value.b;
    }
    return value.a;
}

float distributionGgx(float ndoth, float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(PI * denom * denom, 0.000001);
}

float geometrySchlickGgx(float ndotx, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotx / max(ndotx * (1.0 - k) + k, 0.000001);
}

float geometrySmith(float ndotv, float ndotl, float roughness)
{
    return geometrySchlickGgx(ndotv, roughness) * geometrySchlickGgx(ndotl, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}

vec3 acesToneMap(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 evaluatePbrLight(
    vec3 normal,
    vec3 viewDir,
    vec3 lightDir,
    vec3 radiance,
    vec3 albedo,
    float metallic,
    float roughness)
{
    vec3 halfDir = normalize(lightDir + viewDir);
    float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
    float ndotv = clamp(dot(normal, viewDir), 0.0, 1.0);
    float ndoth = clamp(dot(normal, halfDir), 0.0, 1.0);
    float vdoth = clamp(dot(viewDir, halfDir), 0.0, 1.0);

    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), albedo, metallic);
    vec3 fresnel = fresnelSchlick(vdoth, f0);
    float normalDistribution = distributionGgx(ndoth, roughness);
    float geometry = geometrySmith(ndotv, ndotl, roughness);
    vec3 specular = normalDistribution * geometry * fresnel / max(4.0 * ndotv * ndotl, 0.0001);
    vec3 diffuse = (vec3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic) * albedo / PI;
    return (diffuse + specular) * radiance * ndotl;
}

void main()
{
    vec4 baseColor = texture2D(s_baseColor, v_texcoord0) * u_baseColorFactor * v_color0;
    if (u_alphaParams.x > 0.5 && baseColor.a < u_alphaParams.y) {
        discard;
    }

    float metallic = mix(u_materialParams.x, texture2D(s_metallicMap, v_texcoord0).r, u_materialParams.z);
    float roughness = mix(u_materialParams.y, texture2D(s_roughnessMap, v_texcoord0).r, u_materialParams.w);
    if (u_textureFlags.y > 0.5) {
        vec4 packedMetallicRoughness = texture2D(s_metallicRoughnessMap, v_texcoord0);
        roughness = selectChannel(packedMetallicRoughness, u_packedTextureChannels.x);
        metallic = selectChannel(packedMetallicRoughness, u_packedTextureChannels.y);
    }
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, 0.04, 1.0);
    float occlusion = 1.0;
    if (u_textureFlags.z > 0.5) {
        occlusion = mix(1.0, texture2D(s_occlusionMap, v_texcoord0).r, clamp(u_packedTextureChannels.w, 0.0, 1.0));
    }

    vec3 normal = normalize(v_normal);
    if (u_textureFlags.x > 0.5) {
        vec3 tangent = normalize(v_tangent.xyz);
        vec3 bitangent = normalize(cross(normal, tangent)) * v_tangent.w;
        mat3 tangentToWorld = mat3(tangent, bitangent, normal);
        vec3 sampledNormal = texture2D(s_normalMap, v_texcoord0).xyz * 2.0 - 1.0;
        sampledNormal.xy *= u_packedTextureChannels.z;
        normal = normalize(mul(tangentToWorld, sampledNormal));
    }

    vec3 lightDir = normalize(-u_lightDirection.xyz);
    vec3 viewDir = normalize(u_cameraPosition.xyz - v_worldPos);

    vec3 albedo = baseColor.rgb;
    vec3 sunLight = u_sunColorIntensity.rgb * u_sunColorIntensity.a;
    vec3 directLighting = evaluatePbrLight(normal, viewDir, lightDir, sunLight, albedo, metallic, roughness);
    for (int lightIndex = 0; lightIndex < 16; ++lightIndex) {
        if (float(lightIndex) >= u_pbrParams.z) {
            break;
        }

        vec4 positionRange = u_forwardLightPositionRange[lightIndex];
        vec4 directionType = u_forwardLightDirectionType[lightIndex];
        vec4 colorIntensity = u_forwardLightColorIntensity[lightIndex];
        vec3 authoredLightDir = normalize(-directionType.xyz);
        float attenuation = 1.0;
        if (directionType.w > 0.5) {
            vec3 toLight = positionRange.xyz - v_worldPos;
            float distanceToLight = length(toLight);
            authoredLightDir = distanceToLight > 0.0001 ? toLight / distanceToLight : vec3(0.0, 1.0, 0.0);
            float range = max(positionRange.w, 0.0001);
            float distanceFactor = clamp(1.0 - distanceToLight / range, 0.0, 1.0);
            attenuation = distanceFactor * distanceFactor / max(distanceToLight * distanceToLight, 1.0);
        }
        if (directionType.w > 1.5) {
            vec4 spotParams = u_forwardLightSpotParams[lightIndex];
            vec3 fromLight = normalize(v_worldPos - positionRange.xyz);
            float spotCos = dot(normalize(directionType.xyz), fromLight);
            attenuation *= smoothstep(spotParams.y, spotParams.x, spotCos);
        }

        vec3 radiance = colorIntensity.rgb * colorIntensity.a * attenuation;
        directLighting += evaluatePbrLight(normal, viewDir, authoredLightDir, radiance, albedo, metallic, roughness);
    }

    vec3 ambient = albedo *
        max(u_pbrParams.y, 0.0) *
        u_environmentParams.rgb *
        u_environmentParams.a *
        occlusion;
    vec3 litColor = ambient + directLighting;
    if (u_textureFlags.w > 0.5) {
        litColor += texture2D(s_emissiveMap, v_texcoord0).rgb * u_emissiveFactor.rgb;
    } else {
        litColor += u_emissiveFactor.rgb;
    }

    float fogDistance = distance(u_cameraPosition.xyz, v_worldPos);
    float fogFactor = clamp(1.0 - exp(-fogDistance * max(u_fogParams.x, 0.0)), 0.0, 1.0) * u_fogParams.y;
    vec3 finalColor = mix(litColor, u_fogColor.rgb, fogFactor);
    finalColor = acesToneMap(finalColor * max(u_pbrParams.x, 0.0));
    finalColor = pow(finalColor, vec3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));

    gl_FragColor = vec4(finalColor, baseColor.a);
}
