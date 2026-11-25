#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;
uniform sampler2D texture0; // Automatically bound from MATERIAL_MAP_DIFFUSE
uniform float uTime;
uniform float uWaveHeight;

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragTexCoord;

// Sample two scrolling layers of the noise texture for wave height
float getWaveHeight(vec2 uv) {
    vec2 uv1 = uv * 2.0 + vec2(uTime * 0.03, uTime * 0.02);
    vec2 uv2 = uv * 3.5 + vec2(-uTime * 0.02, uTime * 0.04);
    
    float n1 = texture(texture0, uv1).r;
    float n2 = texture(texture0, uv2).r;
    
    return ((n1 + n2) * 0.5 - 0.5) * uWaveHeight;
}

void main() {
    vec3 p = vertexPosition;
    fragTexCoord = vertexTexCoord;

    // Displace Y using the noise texture
    float h = getWaveHeight(vertexTexCoord);
    p.y += h;

    // Estimate surface normal using neighboring texel heights (finite difference)
    float eps = 0.01;
    float hRight = getWaveHeight(vertexTexCoord + vec2(eps, 0.0));
    float hUp    = getWaveHeight(vertexTexCoord + vec2(0.0, eps));
    
    vec3 normal = normalize(vec3(h - hRight, eps * 15.0, h - hUp));

    fragPosition = vec3(matModel * vec4(p, 1.0));
    fragNormal = normalize(mat3(transpose(inverse(matModel))) * normal);

    gl_Position = mvp * vec4(p, 1.0);
}

