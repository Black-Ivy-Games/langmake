$input v_normal, v_texcoord0, v_color0
#include <bgfx_shader.sh>
SAMPLER2D(s_texColor, 0);
// x: unlit; y: mask cutoff (-1 disables); z: opaque alpha override.
uniform vec4 u_modelMaterial;
void main()
{
    vec4 color = texture2D(s_texColor, v_texcoord0) * v_color0;
    if (u_modelMaterial.y >= 0.0 && color.a < u_modelMaterial.y) discard;
    if (u_modelMaterial.z > 0.5) color.a = 1.0;
    float light = 0.35 + 0.65 * max(dot(normalize(v_normal), normalize(vec3(-0.3, 0.9, 0.2))), 0.0);
    color.rgb *= mix(light, 1.0, u_modelMaterial.x);
    gl_FragColor = color;
}
