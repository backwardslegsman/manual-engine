$input v_texcoord0, v_texcoord1, v_normal, v_tangent, v_worldPos, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_terrainBaseColor0, 0);
SAMPLER2D(s_terrainBaseColor1, 1);
SAMPLER2D(s_terrainBaseColor2, 2);
SAMPLER2D(s_terrainBaseColor3, 3);
SAMPLER2D(s_terrainNormal0, 4);
SAMPLER2D(s_terrainNormal1, 5);
SAMPLER2D(s_terrainNormal2, 6);
SAMPLER2D(s_terrainNormal3, 7);
SAMPLER2D(s_terrainMetallicRoughness0, 8);
SAMPLER2D(s_terrainMetallicRoughness1, 9);
SAMPLER2D(s_terrainMetallicRoughness2, 10);
SAMPLER2D(s_terrainMetallicRoughness3, 11);

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
uniform vec4 u_terrainLayerBaseColor[4];
uniform vec4 u_terrainLayerParams[4];
uniform vec4 u_terrainLayerTiling[4];
uniform vec4 u_terrainRuleParams0[8];
uniform vec4 u_terrainRuleHeightSlope[8];
uniform vec4 u_terrainRuleWorldXz0[8];

static const float PI = 3.14159265359;

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

float rangeWeight(float value, float minValue, float maxValue, float falloff)
{
    if (value < minValue || value > maxValue) {
        return 0.0;
    }
    if (falloff <= 0.0001) {
        return 1.0;
    }
    float nearMin = smoothstep(minValue, minValue + falloff, value);
    float nearMax = 1.0 - smoothstep(maxValue - falloff, maxValue, value);
    return min(nearMin, nearMax);
}

float hasFlag(float value, float flag)
{
    return floor(mod(value / flag, 2.0));
}

vec4 sampleBaseColor(int layer, vec2 uv)
{
    if (layer == 0) {
        return texture2D(s_terrainBaseColor0, uv);
    }
    if (layer == 1) {
        return texture2D(s_terrainBaseColor1, uv);
    }
    if (layer == 2) {
        return texture2D(s_terrainBaseColor2, uv);
    }
    return texture2D(s_terrainBaseColor3, uv);
}

vec3 sampleNormal(int layer, vec2 uv)
{
    if (layer == 0) {
        return texture2D(s_terrainNormal0, uv).xyz;
    }
    if (layer == 1) {
        return texture2D(s_terrainNormal1, uv).xyz;
    }
    if (layer == 2) {
        return texture2D(s_terrainNormal2, uv).xyz;
    }
    return texture2D(s_terrainNormal3, uv).xyz;
}

vec4 sampleMetallicRoughness(int layer, vec2 uv)
{
    if (layer == 0) {
        return texture2D(s_terrainMetallicRoughness0, uv);
    }
    if (layer == 1) {
        return texture2D(s_terrainMetallicRoughness1, uv);
    }
    if (layer == 2) {
        return texture2D(s_terrainMetallicRoughness2, uv);
    }
    return texture2D(s_terrainMetallicRoughness3, uv);
}

void main()
{
    vec3 geometricNormal = normalize(v_normal);
    float slopeDegrees = acos(clamp(geometricNormal.y, -1.0, 1.0)) * 57.2957795131;
    float layerWeights[4];
    layerWeights[0] = 0.0;
    layerWeights[1] = 0.0;
    layerWeights[2] = 0.0;
    layerWeights[3] = 0.0;

    for (int ruleIndex = 0; ruleIndex < 8; ++ruleIndex) {
        vec4 rule = u_terrainRuleParams0[ruleIndex];
        if (rule.x < -0.5) {
            continue;
        }
        int layer = int(clamp(rule.x, 0.0, 3.0));
        float weight = max(rule.y, 0.0);
        float falloff = max(rule.z, 0.0);
        float flags = rule.w;
        vec4 heightSlope = u_terrainRuleHeightSlope[ruleIndex];
        vec4 worldXz = u_terrainRuleWorldXz0[ruleIndex];
        if (hasFlag(flags, 1.0) > 0.5) {
            weight *= rangeWeight(v_worldPos.y, heightSlope.x, heightSlope.y, falloff);
        }
        if (hasFlag(flags, 2.0) > 0.5) {
            weight *= rangeWeight(slopeDegrees, heightSlope.z, heightSlope.w, falloff);
        }
        if (hasFlag(flags, 4.0) > 0.5) {
            weight *= rangeWeight(v_worldPos.x, worldXz.x, worldXz.y, falloff);
        }
        if (hasFlag(flags, 8.0) > 0.5) {
            weight *= rangeWeight(v_worldPos.z, worldXz.z, worldXz.w, falloff);
        }
        layerWeights[layer] += weight;
    }

    float totalWeight = layerWeights[0] + layerWeights[1] + layerWeights[2] + layerWeights[3];
    if (totalWeight <= 0.0001) {
        layerWeights[0] = 1.0;
        totalWeight = 1.0;
    }
    layerWeights[0] /= totalWeight;
    layerWeights[1] /= totalWeight;
    layerWeights[2] /= totalWeight;
    layerWeights[3] /= totalWeight;

    vec4 baseColor = vec4(0.0, 0.0, 0.0, 0.0);
    vec3 blendedNormalSample = vec3(0.0, 0.0, 0.0);
    float metallic = 0.0;
    float roughness = 0.0;
    for (int layer = 0; layer < 4; ++layer) {
        float weight = layerWeights[layer];
        vec4 layerParams = u_terrainLayerParams[layer];
        if (layerParams.w < 0.5 || weight <= 0.0001) {
            continue;
        }
        vec2 uv = v_worldPos.xz * u_terrainLayerTiling[layer].xy;
        vec4 layerColor = sampleBaseColor(layer, uv) * u_terrainLayerBaseColor[layer];
        baseColor += layerColor * weight;
        vec3 sampledNormal = vec3(0.5, 0.5, 1.0);
        if (hasFlag(layerParams.w, 2.0) > 0.5) {
            sampledNormal = sampleNormal(layer, uv);
            sampledNormal.xy = (sampledNormal.xy * 2.0 - 1.0) * layerParams.z;
            sampledNormal.z = sampledNormal.z * 2.0 - 1.0;
        } else {
            sampledNormal = sampledNormal * 2.0 - 1.0;
        }
        blendedNormalSample += sampledNormal * weight;

        float layerMetallic = layerParams.x;
        float layerRoughness = layerParams.y;
        if (hasFlag(layerParams.w, 4.0) > 0.5) {
            vec4 mr = sampleMetallicRoughness(layer, uv);
            layerRoughness = mr.g;
            layerMetallic = mr.b;
        }
        metallic += layerMetallic * weight;
        roughness += layerRoughness * weight;
    }

    baseColor *= v_color0;
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, 0.04, 1.0);

    vec3 normal = geometricNormal;
    vec3 tangent = normalize(v_tangent.xyz);
    vec3 bitangent = normalize(cross(normal, tangent)) * v_tangent.w;
    mat3 tangentToWorld = mat3(tangent, bitangent, normal);
    normal = normalize(mul(tangentToWorld, normalize(blendedNormalSample)));

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
        u_environmentParams.a;
    vec3 litColor = ambient + directLighting;
    float fogDistance = distance(u_cameraPosition.xyz, v_worldPos);
    float fogFactor = clamp(1.0 - exp(-fogDistance * max(u_fogParams.x, 0.0)), 0.0, 1.0) * u_fogParams.y;
    vec3 finalColor = mix(litColor, u_fogColor.rgb, fogFactor);
    finalColor = acesToneMap(finalColor * max(u_pbrParams.x, 0.0));
    finalColor = pow(finalColor, vec3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));

    gl_FragColor = vec4(finalColor, baseColor.a);
}
