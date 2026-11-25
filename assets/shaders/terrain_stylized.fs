#version 330

#define MAX_LIGHTS              4
#define LIGHT_DIRECTIONAL       0
#define LIGHT_POINT             1

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
    float intensity;
};

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec3 fragNormal;

// Output fragment color
out vec4 finalColor;

// Terrain layers
//
// Two layers are used now (grass on flat ground, rock/soil on steep ground).
// To add another layer later:
//   1. add a sampler2D + tint + slope range uniform
//   2. sample it with TerrainLayer()
//   3. blend it with the existing layers using another smoothstep weight.
uniform sampler2D grassMap;
uniform sampler2D rockMap;

uniform vec2 uvScale;       // world-space texture tiles per unit
uniform vec2 slopeRange;    // x: fully rocky, y: fully grassy (normal.y)
uniform vec4 grassTint;
uniform vec4 rockTint;

// Lighting
uniform Light lights[MAX_LIGHTS];
uniform int numOfLights;
uniform vec3 viewPos;
uniform vec3 ambientColor;
uniform float ambientIntensity;

vec3 TerrainLayer(sampler2D map, vec2 uv, vec4 tint)
{
    return texture(map, uv).rgb*tint.rgb;
}

void main()
{
    vec2 uv = fragPosition.xz*uvScale;

    vec3 grass = TerrainLayer(grassMap, uv, grassTint);
    vec3 rock = TerrainLayer(rockMap, uv, rockTint);

    // Flat ground has normal.y close to 1, steep ground close to 0.
    // slopeRange.x is where the surface is fully rock, slopeRange.y where it
    // is fully grass. smoothstep gives a soft, controllable transition.
    vec3 N = normalize(fragNormal);
    float rockWeight = 1.0 - smoothstep(slopeRange.x, slopeRange.y, N.y);
    vec3 albedo = mix(grass, rock, rockWeight);

    vec3 V = normalize(viewPos - fragPosition);
    vec3 lightAccum = vec3(0.0);

    for (int i = 0; i < numOfLights; i++)
    {
        if (lights[i].enabled == 0) continue;

        vec3 L;
        float attenuation;

        if (lights[i].type == LIGHT_DIRECTIONAL)
        {
            // Directional lights have no distance falloff.
            vec3 dir = lights[i].position - lights[i].target;
            if (dot(dir, dir) < 0.000001) continue;
            L = normalize(dir);
            attenuation = 1.0;
        }
        else if (lights[i].type == LIGHT_POINT)
        {
            vec3 toLight = lights[i].position - fragPosition;
            float dist = length(toLight);
            L = toLight/max(dist, 0.0001);
            attenuation = 1.0/max(dist*dist*0.23, 0.0001);
        }
        else
        {
            continue;
        }

        // Soft wrap lighting keeps the stylized look from going completely
        // black on surfaces facing away from the sun.
        float nDotL = max(dot(N, L), 0.0);
        float wrapped = nDotL*0.6 + 0.4;
        lightAccum += lights[i].color.rgb*lights[i].intensity*attenuation*wrapped;
    }

    vec3 color = albedo*(ambientColor*ambientIntensity + lightAccum);

    // Subtle rim highlight, mostly visible on steep rock faces.
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    color += albedo*rim*0.08;

    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    finalColor = vec4(color, 1.0);
}
