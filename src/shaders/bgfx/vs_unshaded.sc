// license:GPLv3+

// Lean vertex shader for the unshaded techniques (additive/unlit primitives, i.e. lightmap overlays). The unshaded fragment
// shader only reads the texture coordinate and, for the ball-shadow variant, the table position, so this shader does the
// position transform and nothing else: no normal transform/normalisation, no view-space position, two interpolants instead
// of four. Output is identical to vs_basic for these techniques; the work per vertex is roughly halved (VLM tables push
// ~2M lightmap vertices per frame). Non-stereo builds only (stereo keeps vs_basic).

$input a_position, a_texcoord0
$output v_tablePos, v_texcoord0
#ifdef CLIP
	$output v_clipDistance
#endif

#include "common.sh"

uniform mat4 matWorld;
uniform mat4 matWorldViewProj;
#ifdef CLIP
	uniform vec4 clip_plane;
#endif

void main()
{
	vec4 pos = vec4(a_position, 1.0);
	vec4 tpos = mul(matWorld, pos);
	v_tablePos = tpos.xyz;
	#ifdef CLIP
		v_clipDistance = dot(clip_plane, tpos);
	#endif
	gl_Position = mul(matWorldViewProj, pos);
	v_texcoord0 = a_texcoord0;
}
