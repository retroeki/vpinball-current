// license:GPLv3+

// Static prepass composite: copies the prepass colour AND depth into the scene target with a fullscreen quad. This replaces
// the per-frame blit of the prepass render target. On a tile-based GPU a blit is a separate transfer pass (read source, write
// destination through memory) followed by a full reload of the destination at the start of the scene pass; a shader copy into
// a cleared target is a single on-tile write. Sampled point-filtered at 1:1, so the result is pixel-identical to the blit,
// depth included (a D24 depth value round-trips exactly through a 32-bit float).

$input v_texcoord0

#include "common.sh"

SAMPLER2D(tex_fb_filtered, 0); // prepass colour
SAMPLER2D(tex_depth, 4);       // prepass depth

void main()
{
	gl_FragColor = texture2DLod(tex_fb_filtered, v_texcoord0, 0.0);
	gl_FragDepth = texture2DLod(tex_depth, v_texcoord0, 0.0).x;
}
