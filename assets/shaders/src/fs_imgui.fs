$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_imgui, 0);

void main()
{
    gl_FragColor = texture2D(s_imgui, v_texcoord0) * v_color0;
}
