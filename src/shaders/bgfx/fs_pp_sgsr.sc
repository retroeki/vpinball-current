// license:GPLv3+ (integration)
//
// Spatial upscaler: Snapdragon Game Super Resolution 1 (SGSR 1), single pass, 12-tap Lanczos-like edge-aware filter with
// adaptive sharpening, used in place of the bilinear copy when the scene is rendered below the output resolution.
// Input is the tonemapped (LDR) frame at render resolution; output is the display resolution.
// w_h_height = (1/w, 1/h, w, h) of the INPUT texture.
//
// Shader core ported from sgsr1_shader_mobile.frag (OperationMode 1 = RGBA, luma taken from the green channel):
//
//   Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
//   SPDX-License-Identifier: BSD-3-Clause
//
//   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
//   following conditions are met: 1. Redistributions of source code must retain the above copyright notice, this list of
//   conditions and the following disclaimer. 2. Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation and/or other materials provided with the
//   distribution. 3. Neither the name of Qualcomm Innovation Center, Inc. nor the names of its contributors may be used to
//   endorse or promote products derived from this software without specific prior written permission.
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
//   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
//   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

$input v_texcoord0

#include "common.sh"

SAMPLER2D(tex_fb_filtered, 0); // render-resolution tonemapped frame (bilinear sampler)

uniform vec4 w_h_height; // 1/w, 1/h, w, h of the input texture

#define EdgeThreshold (8.0 / 255.0)
#define EdgeSharpness 2.0

float fastLanczos2(float x)
{
	float wA = x - 4.0;
	float wB = x * wA - wA;
	wA *= wA;
	return wB * wA;
}

vec2 weightY(float dx, float dy, float c, float std)
{
	float x = ((dx * dx) + (dy * dy)) * 0.55 + clamp(abs(c) * std, 0.0, 1.0);
	float w = fastLanczos2(x);
	return vec2(w, w * c);
}

void main()
{
	vec3 color = texture2DLod(tex_fb_filtered, v_texcoord0, 0.0).xyz;

	vec2 imgCoord = (v_texcoord0 * w_h_height.zw) + vec2(-0.5, 0.5);
	vec2 imgCoordPixel = floor(imgCoord);
	vec2 coord = imgCoordPixel * w_h_height.xy;
	vec2 pl = imgCoord - imgCoordPixel;
	vec4 left = textureGather(tex_fb_filtered, coord, 1);

	float edgeVote = abs(left.z - left.y) + abs(color.y - left.y) + abs(color.y - left.z);
	if (edgeVote > EdgeThreshold)
	{
		coord.x += w_h_height.x;

		vec4 right = textureGather(tex_fb_filtered, coord + vec2(w_h_height.x, 0.0), 1);
		vec4 upDown;
		upDown.xy = textureGather(tex_fb_filtered, coord + vec2(0.0, -w_h_height.y), 1).wz;
		upDown.zw = textureGather(tex_fb_filtered, coord + vec2(0.0,  w_h_height.y), 1).yx;

		float mean = (left.y + left.z + right.x + right.w) * 0.25;
		left = left - vec4_splat(mean);
		right = right - vec4_splat(mean);
		upDown = upDown - vec4_splat(mean);
		float centerY = color.y - mean;

		float sum = (((((abs(left.x) + abs(left.y)) + abs(left.z)) + abs(left.w)) + (((abs(right.x) + abs(right.y)) + abs(right.z)) + abs(right.w))) + (((abs(upDown.x) + abs(upDown.y)) + abs(upDown.z)) + abs(upDown.w)));
		float std = 2.181818 / sum;

		vec2 aWY = weightY(pl.x, pl.y + 1.0, upDown.x, std);
		aWY += weightY(pl.x - 1.0, pl.y + 1.0, upDown.y, std);
		aWY += weightY(pl.x - 1.0, pl.y - 2.0, upDown.z, std);
		aWY += weightY(pl.x, pl.y - 2.0, upDown.w, std);
		aWY += weightY(pl.x + 1.0, pl.y - 1.0, left.x, std);
		aWY += weightY(pl.x, pl.y - 1.0, left.y, std);
		aWY += weightY(pl.x, pl.y, left.z, std);
		aWY += weightY(pl.x + 1.0, pl.y, left.w, std);
		aWY += weightY(pl.x - 1.0, pl.y - 1.0, right.x, std);
		aWY += weightY(pl.x - 2.0, pl.y - 1.0, right.y, std);
		aWY += weightY(pl.x - 2.0, pl.y, right.z, std);
		aWY += weightY(pl.x - 1.0, pl.y, right.w, std);

		float finalY = aWY.y / aWY.x;

		float maxY = max(max(left.y, left.z), max(right.x, right.w));
		float minY = min(min(left.y, left.z), min(right.x, right.w));
		finalY = clamp(EdgeSharpness * finalY, minY, maxY);

		float deltaY = finalY - centerY;
		deltaY = clamp(deltaY, -23.0 / 255.0, 23.0 / 255.0); // smooth high contrast input

		color = clamp(color + vec3_splat(deltaY), vec3_splat(0.0), vec3_splat(1.0));
	}

	gl_FragColor = vec4(color, 1.0);
}
