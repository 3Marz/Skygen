#version 330

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

uniform sampler2D texture0;
uniform float uTime;
uniform vec3 uViewPos;
uniform vec3 uSunDir;

out vec4 finalColor;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(uViewPos - fragPosition);
    vec3 lightDir = normalize(-uSunDir);

    // Base water colors
    vec3 deepColor = vec3(0.01, 0.12, 0.28);
    vec3 skyColor  = vec3(0.45, 0.72, 0.90);

    // Fresnel reflectance
    float fresnel = pow(1.0 - max(dot(viewDir, normal), 0.0), 4.0);
    vec3 waterColor = mix(deepColor, skyColor, fresnel * 0.75);

    // Fine detail ripple from the noise texture
    float detail = texture(texture0, fragTexCoord * 8.0 + uTime * 0.05).r;
    waterColor += vec3(0.03, 0.08, 0.12) * detail;

    // Specular sun glint
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 128.0);
    vec3 specular = vec3(1.0, 0.95, 0.85) * spec * 1.5;

    finalColor = vec4(waterColor + specular, 1.0);
}

