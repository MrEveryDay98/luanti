// NetHack Voxel: the Plane of Water's bubbles where they meet the floor
// (nhv_bubble_floor.cpp). Each bubble is an ellipse; a point inside any of
// them is as far in from the edge of them all as it is from the edge of
// the one it's deepest in - so where two meet, one smooth outline round
// both. Bright at that edge, fading inward; nothing outside.
#define NHV_BUBBLES 32
uniform highp vec4 nhvBubbles[NHV_BUBBLES]; // x, z (nodes), rx, rz
uniform highp float nhvBubbleCount;
uniform lowp vec4 fogColor;
uniform float fogDistance;
uniform float fogShadingParameter;

VARYING_ highp vec2 vWorld;
VARYING_ highp vec3 eyeVec;

void main(void)
{
	highp float depth = -1.0;
	for (int i = 0; i < NHV_BUBBLES; i++) {
		if (float(i) >= nhvBubbleCount)
			break;
		highp vec4 b = nhvBubbles[i];
		highp vec2 d = (vWorld - b.xy) / b.zw;
		highp float q = dot(d, d);
		if (q < 1.0)
			depth = max(depth, (1.0 - sqrt(q)) * min(b.z, b.w));
	}
	if (depth <= 0.0)
		discard;
	// (a crisp bright rim, then a glow fading inward over a few nodes; the
	// very edge softened so it isn't jagged)
	highp float rim = 1.0 - smoothstep(0.0, 0.35, depth);
	highp float glow = 1.0 - smoothstep(0.0, 4.5, depth);
	highp float a = max(rim * 0.9, glow * glow * 0.45) * smoothstep(0.0, 0.06, depth);
	vec4 col = vec4(0.78, 0.96, 1.0, a);

	float clarity = clamp(fogShadingParameter
		- fogShadingParameter * length(eyeVec) / fogDistance, 0.0, 1.0);
	col.rgb = mix(fogColor.rgb, col.rgb, clarity);
	col.a *= clarity;
	gl_FragColor = col;
}
