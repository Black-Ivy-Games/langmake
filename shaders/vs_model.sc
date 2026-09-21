$input a_position, a_normal, a_texcoord0, a_color0
$output v_normal, v_texcoord0, v_color0
#include <bgfx_shader.sh>
// CPU skinning supplies world positions and inverse-transpose transformed
// normals. This keeps pose evidence independent of the graphics backend.
void main()
{
    gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
    v_normal = a_normal;
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
}
