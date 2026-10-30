#define rendered texture0
#define bloom texture1

#ifdef GL_ES
// Dithering requires sufficient floating-point precision
#ifndef GL_FRAGMENT_PRECISION_HIGH
#undef ENABLE_DITHERING
#endif
#endif

struct ExposureParams {
	float compensationFactor;
};

uniform sampler2D rendered;
uniform sampler2D bloom;

uniform vec2 texelSize0;

uniform ExposureParams exposureParams;
uniform lowp float bloomIntensity;
uniform lowp float saturation;
// NetHack Voxel: x = wave strength, y = hallucination, z = time (seconds)
uniform vec3 nhvFx;
// VoxHack retro mode: on, dithering amount, light steps, palette (0 full,
// 1 VGA, 2 EGA) - game.cpp
uniform vec4 nhvRetro;



// the 4x4 ordered dither's threshold at a pixel (0..1)
float nhvBayer(vec2 p)
{
	const float m[16] = float[16](0., 8., 2., 10., 12., 4., 14., 6., 3., 11., 1., 9., 15., 7., 13., 5.);
	int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0));
	return (m[y * 4 + x] + 0.5) / 16.0;
}

// the nearest of VGA's colours (its default palette: EGA's 16, 16 greys, 24
// hues at three saturations and three brightnesses - 246 different), weighted
// by how bright each channel looks. Written out, not a table looked up in a
// loop: GPUs index a big constant table very slowly.
#define NHV_TRY(r, g, b) { vec3 p_ = vec3(r, g, b); vec3 d_ = c - p_; float e_ = dot(w, d_ * d_); if (e_ < bd) { bd = e_; best = p_; } }
// the nearest of EGA's 16 colours (NetHack's own) - each channel counted
// alike: with so few colours, weighting by brightness turns everything
// grey; this keeps the hue
vec3 nhvEga(vec3 c)
{
	const vec3 w = vec3(1.0);
	vec3 best = vec3(0.0);
	float bd = 1e9;
	NHV_TRY(0.000, 0.000, 0.000) NHV_TRY(0.000, 0.000, 0.667) NHV_TRY(0.000, 0.667, 0.000) NHV_TRY(0.000, 0.667, 0.667)
	NHV_TRY(0.667, 0.000, 0.000) NHV_TRY(0.667, 0.000, 0.667) NHV_TRY(0.667, 0.333, 0.000) NHV_TRY(0.667, 0.667, 0.667)
	NHV_TRY(0.333, 0.333, 0.333) NHV_TRY(0.333, 0.333, 1.000) NHV_TRY(0.333, 1.000, 0.333) NHV_TRY(0.333, 1.000, 1.000)
	NHV_TRY(1.000, 0.333, 0.333) NHV_TRY(1.000, 0.333, 1.000) NHV_TRY(1.000, 1.000, 0.333) NHV_TRY(1.000, 1.000, 1.000)
	return best;
}

vec3 nhvVga(vec3 c)
{
	const vec3 w = vec3(0.30, 0.59, 0.11);
	vec3 best = vec3(0.0);
	float bd = 1e9;
	NHV_TRY(0.000, 0.000, 0.000) NHV_TRY(0.000, 0.000, 0.667) NHV_TRY(0.000, 0.667, 0.000)
	NHV_TRY(0.000, 0.667, 0.667) NHV_TRY(0.667, 0.000, 0.000) NHV_TRY(0.667, 0.000, 0.667)
	NHV_TRY(0.667, 0.333, 0.000) NHV_TRY(0.667, 0.667, 0.667) NHV_TRY(0.333, 0.333, 0.333)
	NHV_TRY(0.333, 0.333, 1.000) NHV_TRY(0.333, 1.000, 0.333) NHV_TRY(0.333, 1.000, 1.000)
	NHV_TRY(1.000, 0.333, 0.333) NHV_TRY(1.000, 0.333, 1.000) NHV_TRY(1.000, 1.000, 0.333)
	NHV_TRY(1.000, 1.000, 1.000) NHV_TRY(0.079, 0.079, 0.079) NHV_TRY(0.127, 0.127, 0.127)
	NHV_TRY(0.175, 0.175, 0.175) NHV_TRY(0.222, 0.222, 0.222) NHV_TRY(0.270, 0.270, 0.270)
	NHV_TRY(0.317, 0.317, 0.317) NHV_TRY(0.381, 0.381, 0.381) NHV_TRY(0.444, 0.444, 0.444)
	NHV_TRY(0.508, 0.508, 0.508) NHV_TRY(0.571, 0.571, 0.571) NHV_TRY(0.635, 0.635, 0.635)
	NHV_TRY(0.714, 0.714, 0.714) NHV_TRY(0.794, 0.794, 0.794) NHV_TRY(0.889, 0.889, 0.889)
	NHV_TRY(0.000, 0.000, 1.000) NHV_TRY(0.254, 0.000, 1.000) NHV_TRY(0.492, 0.000, 1.000)
	NHV_TRY(0.746, 0.000, 1.000) NHV_TRY(1.000, 0.000, 1.000) NHV_TRY(1.000, 0.000, 0.746)
	NHV_TRY(1.000, 0.000, 0.492) NHV_TRY(1.000, 0.000, 0.254) NHV_TRY(1.000, 0.000, 0.000)
	NHV_TRY(1.000, 0.254, 0.000) NHV_TRY(1.000, 0.492, 0.000) NHV_TRY(1.000, 0.746, 0.000)
	NHV_TRY(1.000, 1.000, 0.000) NHV_TRY(0.746, 1.000, 0.000) NHV_TRY(0.492, 1.000, 0.000)
	NHV_TRY(0.254, 1.000, 0.000) NHV_TRY(0.000, 1.000, 0.000) NHV_TRY(0.000, 1.000, 0.254)
	NHV_TRY(0.000, 1.000, 0.492) NHV_TRY(0.000, 1.000, 0.746) NHV_TRY(0.000, 1.000, 1.000)
	NHV_TRY(0.000, 0.746, 1.000) NHV_TRY(0.000, 0.492, 1.000) NHV_TRY(0.000, 0.254, 1.000)
	NHV_TRY(0.492, 0.492, 1.000) NHV_TRY(0.619, 0.492, 1.000) NHV_TRY(0.746, 0.492, 1.000)
	NHV_TRY(0.873, 0.492, 1.000) NHV_TRY(1.000, 0.492, 1.000) NHV_TRY(1.000, 0.492, 0.873)
	NHV_TRY(1.000, 0.492, 0.746) NHV_TRY(1.000, 0.492, 0.619) NHV_TRY(1.000, 0.492, 0.492)
	NHV_TRY(1.000, 0.619, 0.492) NHV_TRY(1.000, 0.746, 0.492) NHV_TRY(1.000, 0.873, 0.492)
	NHV_TRY(1.000, 1.000, 0.492) NHV_TRY(0.873, 1.000, 0.492) NHV_TRY(0.746, 1.000, 0.492)
	NHV_TRY(0.619, 1.000, 0.492) NHV_TRY(0.492, 1.000, 0.492) NHV_TRY(0.492, 1.000, 0.619)
	NHV_TRY(0.492, 1.000, 0.746) NHV_TRY(0.492, 1.000, 0.873) NHV_TRY(0.492, 1.000, 1.000)
	NHV_TRY(0.492, 0.873, 1.000) NHV_TRY(0.492, 0.746, 1.000) NHV_TRY(0.492, 0.619, 1.000)
	NHV_TRY(0.714, 0.714, 1.000) NHV_TRY(0.778, 0.714, 1.000) NHV_TRY(0.857, 0.714, 1.000)
	NHV_TRY(0.921, 0.714, 1.000) NHV_TRY(1.000, 0.714, 1.000) NHV_TRY(1.000, 0.714, 0.921)
	NHV_TRY(1.000, 0.714, 0.857) NHV_TRY(1.000, 0.714, 0.778) NHV_TRY(1.000, 0.714, 0.714)
	NHV_TRY(1.000, 0.778, 0.714) NHV_TRY(1.000, 0.857, 0.714) NHV_TRY(1.000, 0.921, 0.714)
	NHV_TRY(1.000, 1.000, 0.714) NHV_TRY(0.921, 1.000, 0.714) NHV_TRY(0.857, 1.000, 0.714)
	NHV_TRY(0.778, 1.000, 0.714) NHV_TRY(0.714, 1.000, 0.714) NHV_TRY(0.714, 1.000, 0.778)
	NHV_TRY(0.714, 1.000, 0.857) NHV_TRY(0.714, 1.000, 0.921) NHV_TRY(0.714, 1.000, 1.000)
	NHV_TRY(0.714, 0.921, 1.000) NHV_TRY(0.714, 0.857, 1.000) NHV_TRY(0.714, 0.778, 1.000)
	NHV_TRY(0.000, 0.000, 0.444) NHV_TRY(0.111, 0.000, 0.444) NHV_TRY(0.222, 0.000, 0.444)
	NHV_TRY(0.333, 0.000, 0.444) NHV_TRY(0.444, 0.000, 0.444) NHV_TRY(0.444, 0.000, 0.333)
	NHV_TRY(0.444, 0.000, 0.222) NHV_TRY(0.444, 0.000, 0.111) NHV_TRY(0.444, 0.000, 0.000)
	NHV_TRY(0.444, 0.111, 0.000) NHV_TRY(0.444, 0.222, 0.000) NHV_TRY(0.444, 0.333, 0.000)
	NHV_TRY(0.444, 0.444, 0.000) NHV_TRY(0.333, 0.444, 0.000) NHV_TRY(0.222, 0.444, 0.000)
	NHV_TRY(0.111, 0.444, 0.000) NHV_TRY(0.000, 0.444, 0.000) NHV_TRY(0.000, 0.444, 0.111)
	NHV_TRY(0.000, 0.444, 0.222) NHV_TRY(0.000, 0.444, 0.333) NHV_TRY(0.000, 0.444, 0.444)
	NHV_TRY(0.000, 0.333, 0.444) NHV_TRY(0.000, 0.222, 0.444) NHV_TRY(0.000, 0.111, 0.444)
	NHV_TRY(0.222, 0.222, 0.444) NHV_TRY(0.270, 0.222, 0.444) NHV_TRY(0.333, 0.222, 0.444)
	NHV_TRY(0.381, 0.222, 0.444) NHV_TRY(0.444, 0.222, 0.444) NHV_TRY(0.444, 0.222, 0.381)
	NHV_TRY(0.444, 0.222, 0.333) NHV_TRY(0.444, 0.222, 0.270) NHV_TRY(0.444, 0.222, 0.222)
	NHV_TRY(0.444, 0.270, 0.222) NHV_TRY(0.444, 0.333, 0.222) NHV_TRY(0.444, 0.381, 0.222)
	NHV_TRY(0.444, 0.444, 0.222) NHV_TRY(0.381, 0.444, 0.222) NHV_TRY(0.333, 0.444, 0.222)
	NHV_TRY(0.270, 0.444, 0.222) NHV_TRY(0.222, 0.444, 0.222) NHV_TRY(0.222, 0.444, 0.270)
	NHV_TRY(0.222, 0.444, 0.333) NHV_TRY(0.222, 0.444, 0.381) NHV_TRY(0.222, 0.444, 0.444)
	NHV_TRY(0.222, 0.381, 0.444) NHV_TRY(0.222, 0.333, 0.444) NHV_TRY(0.222, 0.270, 0.444)
	NHV_TRY(0.317, 0.317, 0.444) NHV_TRY(0.349, 0.317, 0.444) NHV_TRY(0.381, 0.317, 0.444)
	NHV_TRY(0.413, 0.317, 0.444) NHV_TRY(0.444, 0.317, 0.444) NHV_TRY(0.444, 0.317, 0.413)
	NHV_TRY(0.444, 0.317, 0.381) NHV_TRY(0.444, 0.317, 0.349) NHV_TRY(0.444, 0.317, 0.317)
	NHV_TRY(0.444, 0.349, 0.317) NHV_TRY(0.444, 0.381, 0.317) NHV_TRY(0.444, 0.413, 0.317)
	NHV_TRY(0.444, 0.444, 0.317) NHV_TRY(0.413, 0.444, 0.317) NHV_TRY(0.381, 0.444, 0.317)
	NHV_TRY(0.349, 0.444, 0.317) NHV_TRY(0.317, 0.444, 0.317) NHV_TRY(0.317, 0.444, 0.349)
	NHV_TRY(0.317, 0.444, 0.381) NHV_TRY(0.317, 0.444, 0.413) NHV_TRY(0.317, 0.444, 0.444)
	NHV_TRY(0.317, 0.413, 0.444) NHV_TRY(0.317, 0.381, 0.444) NHV_TRY(0.317, 0.349, 0.444)
	NHV_TRY(0.000, 0.000, 0.254) NHV_TRY(0.063, 0.000, 0.254) NHV_TRY(0.127, 0.000, 0.254)
	NHV_TRY(0.190, 0.000, 0.254) NHV_TRY(0.254, 0.000, 0.254) NHV_TRY(0.254, 0.000, 0.190)
	NHV_TRY(0.254, 0.000, 0.127) NHV_TRY(0.254, 0.000, 0.063) NHV_TRY(0.254, 0.000, 0.000)
	NHV_TRY(0.254, 0.063, 0.000) NHV_TRY(0.254, 0.127, 0.000) NHV_TRY(0.254, 0.190, 0.000)
	NHV_TRY(0.254, 0.254, 0.000) NHV_TRY(0.190, 0.254, 0.000) NHV_TRY(0.127, 0.254, 0.000)
	NHV_TRY(0.063, 0.254, 0.000) NHV_TRY(0.000, 0.254, 0.000) NHV_TRY(0.000, 0.254, 0.063)
	NHV_TRY(0.000, 0.254, 0.127) NHV_TRY(0.000, 0.254, 0.190) NHV_TRY(0.000, 0.254, 0.254)
	NHV_TRY(0.000, 0.190, 0.254) NHV_TRY(0.000, 0.127, 0.254) NHV_TRY(0.000, 0.063, 0.254)
	NHV_TRY(0.127, 0.127, 0.254) NHV_TRY(0.159, 0.127, 0.254) NHV_TRY(0.190, 0.127, 0.254)
	NHV_TRY(0.222, 0.127, 0.254) NHV_TRY(0.254, 0.127, 0.254) NHV_TRY(0.254, 0.127, 0.222)
	NHV_TRY(0.254, 0.127, 0.190) NHV_TRY(0.254, 0.127, 0.159) NHV_TRY(0.254, 0.127, 0.127)
	NHV_TRY(0.254, 0.159, 0.127) NHV_TRY(0.254, 0.190, 0.127) NHV_TRY(0.254, 0.222, 0.127)
	NHV_TRY(0.254, 0.254, 0.127) NHV_TRY(0.222, 0.254, 0.127) NHV_TRY(0.190, 0.254, 0.127)
	NHV_TRY(0.159, 0.254, 0.127) NHV_TRY(0.127, 0.254, 0.127) NHV_TRY(0.127, 0.254, 0.159)
	NHV_TRY(0.127, 0.254, 0.190) NHV_TRY(0.127, 0.254, 0.222) NHV_TRY(0.127, 0.254, 0.254)
	NHV_TRY(0.127, 0.222, 0.254) NHV_TRY(0.127, 0.190, 0.254) NHV_TRY(0.127, 0.159, 0.254)
	NHV_TRY(0.175, 0.175, 0.254) NHV_TRY(0.190, 0.175, 0.254) NHV_TRY(0.206, 0.175, 0.254)
	NHV_TRY(0.238, 0.175, 0.254) NHV_TRY(0.254, 0.175, 0.254) NHV_TRY(0.254, 0.175, 0.238)
	NHV_TRY(0.254, 0.175, 0.206) NHV_TRY(0.254, 0.175, 0.190) NHV_TRY(0.254, 0.175, 0.175)
	NHV_TRY(0.254, 0.190, 0.175) NHV_TRY(0.254, 0.206, 0.175) NHV_TRY(0.254, 0.238, 0.175)
	NHV_TRY(0.254, 0.254, 0.175) NHV_TRY(0.238, 0.254, 0.175) NHV_TRY(0.206, 0.254, 0.175)
	NHV_TRY(0.190, 0.254, 0.175) NHV_TRY(0.175, 0.254, 0.175) NHV_TRY(0.175, 0.254, 0.190)
	NHV_TRY(0.175, 0.254, 0.206) NHV_TRY(0.175, 0.254, 0.238) NHV_TRY(0.175, 0.254, 0.254)
	NHV_TRY(0.175, 0.238, 0.254) NHV_TRY(0.175, 0.206, 0.254) NHV_TRY(0.175, 0.190, 0.254)
	return best;
}


CENTROID_ VARYING_ mediump vec2 varTexCoord;

#ifdef ENABLE_AUTO_EXPOSURE
VARYING_ float exposure; // linear exposure factor, see vertex shader
#endif

#ifdef ENABLE_BLOOM

vec4 applyBloom(vec4 color, vec2 uv)
{
	vec3 light = texture2D(bloom, uv).rgb;
#ifdef ENABLE_BLOOM_DEBUG
	if (uv.x > 0.5 && uv.y < 0.5)
		return vec4(light, color.a);
	if (uv.x < 0.5)
		return color;
#endif
	color.rgb = mix(color.rgb, light, bloomIntensity);
	return color;
}

#endif

#if ENABLE_TONE_MAPPING

/* Hable's UC2 Tone mapping parameters
	A = 0.22;
	B = 0.30;
	C = 0.10;
	D = 0.20;
	E = 0.01;
	F = 0.30;
	W = 11.2;
	equation used:  ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F
*/

// highp for GLES, see <https://github.com/luanti-org/luanti/pull/14688>
highp vec3 uncharted2Tonemap(highp vec3 x)
{
	return ((x * (0.22 * x + 0.03) + 0.002) / (x * (0.22 * x + 0.3) + 0.06)) - 0.03333;
}

vec4 applyToneMapping(vec4 color)
{
	color = vec4(pow(color.rgb, vec3(2.2)), color.a);
	const float gamma = 1.6;
	const float exposureBias = 5.5;
	color.rgb = uncharted2Tonemap(exposureBias * color.rgb);
	// Precalculated white_scale from
	//vec3 whiteScale = 1.0 / uncharted2Tonemap(vec3(W));
	vec3 whiteScale = vec3(1.036015346);
	color.rgb *= whiteScale;
	return vec4(pow(color.rgb, vec3(1.0 / gamma)), color.a);
}
#endif

vec3 applySaturation(vec3 color, float factor)
{
	// Calculate the perceived luminosity from the RGB color.
	// See also: https://www.w3.org/WAI/GL/wiki/Relative_luminance
	float brightness = dot(color, vec3(0.2125, 0.7154, 0.0721));
	return mix(vec3(brightness), color, factor);
}

// NetHack Voxel: turn a colour's hue by angle a (radians)
vec3 nhvHueShift(vec3 c, float a)
{
	const vec3 k = vec3(0.57735);
	float ca = cos(a);
	return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

#ifdef ENABLE_DITHERING
// From http://alex.vlachos.com/graphics/Alex_Vlachos_Advanced_VR_Rendering_GDC2015.pdf
// and https://www.shadertoy.com/view/MslGR8 (5th one starting from the bottom)
// NOTE: `frag_coord` is in pixels (i.e. not normalized UV).
vec3 screen_space_dither(highp vec2 frag_coord) {
	// Iestyn's RGB dither (7 asm instructions) from Portal 2 X360, slightly modified for VR.
	highp vec3 dither = vec3(dot(vec2(171.0, 231.0), frag_coord));
	dither.rgb = fract(dither.rgb / vec3(103.0, 71.0, 97.0));

	// Subtract 0.5 to avoid slightly brightening the whole viewport.
	return (dither.rgb - 0.5) / 255.0;
}
#endif

void main(void)
{
	vec2 uv = varTexCoord.st;
	// NetHack Voxel: confused / stunned / hallucinating, the picture waves
	if (nhvFx.x > 0.0) {
		float t = nhvFx.z;
		uv += nhvFx.x * 0.006 * vec2(sin(uv.y * 17.0 + t * 1.9), cos(uv.x * 13.0 + t * 1.4));
		uv = clamp(uv, vec2(0.001), vec2(0.999));
	}
#ifdef ENABLE_SSAA
	vec4 color = vec4(0.);
	for (float dx = 1.; dx < SSAA_SCALE; dx += 2.)
	for (float dy = 1.; dy < SSAA_SCALE; dy += 2.)
		color += texture2D(rendered, uv + texelSize0 * vec2(dx, dy)).rgba;
	color /= SSAA_SCALE * SSAA_SCALE / 4.;
#else
	vec4 color = texture2D(rendered, uv).rgba;
#endif

	// translate to linear colorspace (approximate)
	color.rgb = pow(color.rgb, vec3(2.2));

#ifdef ENABLE_BLOOM_DEBUG
	if (uv.x > 0.5 || uv.y > 0.5)
#endif
	{
		color.rgb *= exposureParams.compensationFactor;
#ifdef ENABLE_AUTO_EXPOSURE
		color.rgb *= exposure;
#endif
	}

#ifdef ENABLE_BLOOM
	color = applyBloom(color, uv);
#endif


	color.rgb = clamp(color.rgb, vec3(0.), vec3(1.));

	// return to sRGB colorspace (approximate)
	color.rgb = pow(color.rgb, vec3(1.0 / 2.2));

#ifdef ENABLE_BLOOM_DEBUG
	if (uv.x > 0.5 || uv.y > 0.5)
#endif
	{
#if ENABLE_TONE_MAPPING
		color = applyToneMapping(color);
#endif

		color.rgb = applySaturation(color.rgb, saturation);
	}

	// NetHack Voxel: hallucinating, the colours slowly drift round the hue
	// circle (and get a little more vivid)
	if (nhvFx.y > 0.0) {
		vec3 shifted = clamp(nhvHueShift(color.rgb, nhvFx.z * 0.35 + varTexCoord.x * 0.6), 0.0, 1.0);
		shifted = applySaturation(shifted, 1.35);
		color.rgb = mix(color.rgb, clamp(shifted, 0.0, 1.0), nhvFx.y);
	}

	// VoxHack retro mode: its palette's colours - dithered, the ordered
	// pattern standing in for the colours between (wider for EGA's few)
	if (nhvRetro.x > 0.0 && nhvRetro.w > 0.0) {
		float spread = nhvRetro.w > 1.5 ? 0.36 : 0.12;
		if (nhvRetro.y > 0.0)
			color.rgb = clamp(color.rgb + (nhvBayer(gl_FragCoord.xy) - 0.5) * spread * nhvRetro.y, 0.0, 1.0);
		gl_FragColor = vec4(nhvRetro.w > 1.5 ? nhvEga(color.rgb) : nhvVga(color.rgb), 1.0);
		return;
	}

#ifdef ENABLE_DITHERING
	// Apply dithering just before quantisation
	color.rgb += screen_space_dither(gl_FragCoord.xy);
#endif

	gl_FragColor = vec4(color.rgb, 1.0); // force full alpha to avoid holes in the image.
}
