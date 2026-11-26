#pragma usage opaque shadow

// Stylized terrain surface shader for R3D.
// R3D supplies the world-space POSITION/NORMAL and handles all lighting.
// Add another layer by adding a sampler + tint/slope uniforms and mixing it
// into ALBEDO the same way.

uniform sampler2D grassMap;
uniform sampler2D rockMap;

uniform vec2 uvScale;
uniform vec2 slopeRange;    // x: fully rock, y: fully grass (normal.y)
uniform vec3 grassTint;
uniform vec3 rockTint;

void fragment()
{
    vec2 uv = POSITION.xz*uvScale;

    vec3 grass = texture(grassMap, uv).rgb*grassTint;
    vec3 rock = texture(rockMap, uv).rgb*rockTint;

    vec3 n = normalize(NORMAL);
    float rockWeight = 1.0 - smoothstep(slopeRange.x, slopeRange.y, n.y);

    ALBEDO = mix(grass, rock, rockWeight);
    ROUGHNESS = mix(0.90, 0.82, rockWeight);
    METALNESS = 0.0;
    SPECULAR = 0.5;
}
