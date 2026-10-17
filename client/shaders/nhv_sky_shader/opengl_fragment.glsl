// NetHack Voxel: the sky's clouds - one flat plane high overhead, drawn
// see-through over the sky behind it (a plain colour, or the day and
// night sky). Two layers, each either a picture or clouds generated here
// (layered noise), scrolling at their own speeds; the front one over the
// back one. Towards the horizon they fade out (and into the horizon colour).
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform highp vec4 nhvSkyLayer0;  // scroll offset x, y; scale; opacity
uniform highp vec4 nhvSkyLayer1;
uniform lowp vec4 nhvSkyTint0;    // rgb colour; a = black is see-through
uniform lowp vec4 nhvSkyTint1;
uniform highp vec4 nhvSkyGen0;    // generated?; coverage; softness; detail (octaves)
uniform highp vec4 nhvSkyGen1;
uniform lowp vec4 nhvSkyShade0;   // rgb: the clouds' shadowed parts
uniform lowp vec4 nhvSkyShade1;
uniform lowp vec4 nhvSkyHorizon;  // rgb; a = how strongly it fades in
uniform highp vec4 nhvSkyMisc;    // x: pixels per repeat (0 = smooth); y: brightness

VARYING_ highp vec3 vPos;

highp float hash(highp vec2 p)
{
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}

highp float noise(highp vec2 p)
{
	highp vec2 i = floor(p);
	highp vec2 f = fract(p);
	highp vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
		mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

highp float fbm(highp vec2 p, float octaves)
{
	highp float v = 0.0, amp = 0.5, total = 0.0;
	for (int k = 0; k < 6; k++) {
		if (float(k) >= octaves)
			break;
		v += noise(p) * amp;
		total += amp;
		p = p * 2.03 + vec2(17.1, 9.7);
		amp *= 0.5;
	}
	return v / max(total, 0.001);
}

// one layer: rgb and how much of it there is
vec4 layer(sampler2D tex, highp vec2 p, highp vec4 L, lowp vec4 tint, highp vec4 gen, lowp vec4 shade)
{
	highp vec2 uv = p * L.z + L.xy;
	if (nhvSkyMisc.x > 0.0)
		uv = (floor(uv * nhvSkyMisc.x) + 0.5) / nhvSkyMisc.x; // (sharp pixels)
	if (gen.x > 0.5) {
		highp float n = fbm(uv * 4.0, gen.w);
		highp float edge = 1.0 - gen.y;
		float a = smoothstep(edge, edge + max(gen.z, 0.01), n);
		// (thicker parts lighter, thin edges in shadow)
		vec3 col = mix(shade.rgb, tint.rgb, smoothstep(edge, edge + max(gen.z, 0.01) * 2.0 + 0.15, n));
		return vec4(col, a * L.w);
	}
	vec4 c = texture2D(tex, uv);
	float a = c.a * L.w;
	if (tint.a > 0.5)
		a *= step(0.02, max(c.r, max(c.g, c.b)));
	return vec4(c.rgb * tint.rgb, a);
}

void main(void)
{
	highp vec3 d = normalize(vPos);
	// (on the plane: its height is 1, so these are its coordinates)
	highp vec2 p = vPos.xz / vPos.y * 0.25;

	vec4 back = layer(texture0, p, nhvSkyLayer0, nhvSkyTint0, nhvSkyGen0, nhvSkyShade0);
	vec4 front = layer(texture1, p, nhvSkyLayer1, nhvSkyTint1, nhvSkyGen1, nhvSkyShade1);

	float a = back.a + front.a * (1.0 - back.a);
	vec3 col = (back.rgb * back.a * (1.0 - front.a) + front.rgb * front.a) / max(a, 0.001);

	// towards the horizon: into the horizon colour, then gone
	// (low down a flat plane is stretched out into streaks: gone by then)
	float low = 1.0 - smoothstep(0.05, 0.45, d.y);
	col = mix(col, nhvSkyHorizon.rgb, low * nhvSkyHorizon.a);
	a *= smoothstep(0.04, 0.28, d.y);

	gl_FragColor = vec4(col * nhvSkyMisc.y, a);
}
