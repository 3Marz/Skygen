#version 330

// Input vertex attributes
in vec3 vertexPosition;
in vec3 vertexNormal;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

// Output vertex attributes (to fragment shader)
out vec3 fragPosition;
out vec3 fragNormal;

void main()
{
    // World-space position and normal are all the stylized terrain shader
    // needs. Texture coordinates are derived from the world position so the
    // layers stay continuous across chunk and LOD borders.
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));

    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
