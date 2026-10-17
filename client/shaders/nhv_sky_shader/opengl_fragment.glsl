// NetHack Voxel: Quake-style sky. Two texture layers drawn on a flattened
// dome over you (as Quake's sky: the view direction with its height
// stretched 3x, projected onto the horizontal plane), each scrolling at its
// own speed. The back layer is drawn over black, the front one over it
// (where black counts as see-through if asked); towards the horizon it
// fades into the horizon colour.
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform highp vec4 nhvSkyLayer0;  // scroll offset x, y; scale; opacity
uniform highp vec4 nhvSkyLayer1;
uniform lowp vec4 nhvSkyTint0;    // rgb tint
uniform lowp vec4 nhvSkyTint1;    // rgb tint; a = black is see-through
uniform lowp vec4 nhvSkyHorizon;  // rgb; a = how strongly it fades in

VARYING_ highp vec3 vDir;

void main(void)
{
	highp vec3 d = normalize(vDir);
	highp vec3 f = vec3(d.x, abs(d.y) * 3.0, d.z);
	highp vec2 p = d.xz / length(f);

	vec4 back = texture2D(texture0, p * nhvSkyLayer0.z + nhvSkyLayer0.xy);
	vec4 front = texture2D(texture1, p * nhvSkyLayer1.z + nhvSkyLayer1.xy);

	vec3 col = back.rgb * nhvSkyTint0.rgb * (back.a * nhvSkyLayer0.w);
	float a1 = front.a * nhvSkyLayer1.w;
	if (nhvSkyTint1.a > 0.5)
		a1 *= step(0.02, max(front.r, max(front.g, front.b)));
	col = mix(col, front.rgb * nhvSkyTint1.rgb, a1);

	float h = 1.0 - smoothstep(0.0, 0.35, abs(d.y));
	col = mix(col, nhvSkyHorizon.rgb, h * nhvSkyHorizon.a);
	gl_FragColor = vec4(col, 1.0);
}
