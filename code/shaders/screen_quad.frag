#version 450

layout(location = 0) out vec4 final_color;

layout(location = 0) in vec2 in_uvs;

layout(set = 0, binding = 0) uniform sampler2D tex;

float linearize_depth(float depth)
{
    float n = 1.0; // camera z near
    float f = 100000.0; // camera z far
    float z = depth;
    return (2.0 * n) / (f + n - depth * (f - n));	
}

void
main(void)
{
    float d = texture(tex, in_uvs).r;

    final_color = vec4(linearize_depth(d));
//    final_color = vec4(1 - d) * 10;
}
,
