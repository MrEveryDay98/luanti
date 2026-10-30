// VoxHack: a thing's shadow on the floor. The sheet is a box round all the
// floor it can fall on (content_cao.cpp's nhvLayOutShadow, which also puts
// what's needed here in the matrix's third column). Each of the floor's
// texture pixels (a 16th of a block) is in the shadow or not, whole: from
// the pixel's middle, toward the sun, does the way pass through the thing -
// its picture as a voxel model, its pixels blocks as thick as it is (a flat
// sprite: not thick)?
#define NHV_TEXEL (10.0 / 16.0)
#define NHV_PICTURE_PX 16.0
#define NHV_MAX_STEPS 48

uniform mat4 mWorld;
uniform sampler2D baseTexture;
uniform vec3 cameraOffset;
uniform lowp vec4 fogColor;
uniform float fogDistance;
uniform float fogShadingParameter;
uniform float nhvFogMax;

VARYING_ highp vec3 worldPosition;
VARYING_ highp vec2 vHalf;
VARYING_ highp vec3 eyeVec;

void main(void)
{
	// (the floor pixel's middle, here near the camera: cameraOffset)
	highp vec2 p = worldPosition.xz + cameraOffset.xz;
	highp vec2 c = (floor(p / NHV_TEXEL) + 0.5) * NHV_TEXEL - cameraOffset.xz;

	highp float yaw = mWorld[2].x;
	highp vec2 across = vec2(cos(yaw), sin(yaw)); // (the picture's left to right)
	highp vec2 normal = vec2(-across.y, across.x); // (its thickness)
	highp vec2 along = mWorld[2].yz; // (a point's shadow moves this far a unit up)
	highp float thick = mWorld[2].w;
	highp float w = 2.0 * vHalf.x, h = 2.0 * vHalf.y;
	highp vec2 foot = mWorld[3].xz - along * vHalf.y;

	// a point z up the way to the sun from here is above rel - along * z:
	// in the model while it's within its thickness (z from za to zb)
	highp vec2 rel = c - foot;
	highp float dn = dot(rel, normal), k = dot(along, normal);
	highp float za = 0.0, zb = h;
	if (abs(k) > 1e-5) {
		highp float z1 = (dn - thick * 0.5) / k, z2 = (dn + thick * 0.5) / k;
		za = max(min(z1, z2), 0.0);
		zb = min(max(z1, z2), h);
	} else if (abs(dn) > thick * 0.5) {
		discard;
	}
	if (zb < za)
		discard;
	// (looked at every half picture pixel on the way through it)
	highp float x0 = dot(rel, across), ka = dot(along, across);
	highp float span = max(abs(ka) * (zb - za) / (w / NHV_PICTURE_PX), (zb - za) / (h / NHV_PICTURE_PX));
	int steps = int(clamp(ceil(span * 2.0) + 1.0, 1.0, float(NHV_MAX_STEPS)));
	// (an extruded model's picture runs left to right across it, a flat
	// sprite's the other way: content_cao's meshes)
	highp float mirror = thick > 0.0 ? 1.0 : -1.0;
	float a = 0.0;
	for (int i = 0; i < NHV_MAX_STEPS; i++) {
		if (i >= steps)
			break;
		highp float z = za + (zb - za) * (float(i) + 0.5) / float(steps);
		highp vec2 uv = vec2(0.5 + mirror * (x0 - ka * z) / w, 1.0 - z / h);
		if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
			// (the picture itself, never a smaller copy: where the lookup
			// jumps a pixel, at the floor pixel's edges, the GPU would
			// take one)
			float s = textureLod(baseTexture, uv, 0.0).a;
			if (s > 0.01) {
				a = s;
				break;
			}
		}
	}
	if (a <= 0.0)
		discard;
	float clarity = max(clamp(fogShadingParameter
		- fogShadingParameter * length(eyeVec) / fogDistance, 0.0, 1.0), 1.0 - nhvFogMax);
	gl_FragColor = vec4(0.0, 0.0, 0.0, a * clarity);
}
