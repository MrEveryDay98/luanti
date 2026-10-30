// VoxHack: a thing's shadow on the floor. The sheet is its picture
// projected flat from the light (content_cao.cpp lays it out), a little
// bigger than the picture all round (NHV_PAD). Each of the floor's texture
// pixels (a 16th of a block) is in the shadow or not, whole: the picture is
// looked up at the pixel's middle - where that is on the sheet, from the
// sheet's own layout (mWorld) - not here.
#define NHV_PAD 1.5
#define NHV_TEXEL (10.0 / 16.0)

uniform mat4 mWorld;
uniform sampler2D baseTexture;
uniform vec3 cameraOffset;
uniform lowp vec4 fogColor;
uniform float fogDistance;
uniform float fogShadingParameter;

VARYING_ highp vec3 worldPosition;
VARYING_ highp vec4 vSheet;
VARYING_ highp vec3 eyeVec;

void main(void)
{
	// (the floor pixel's middle, here near the camera: cameraOffset)
	highp vec2 p = worldPosition.xz + cameraOffset.xz;
	highp vec2 c = (floor(p / NHV_TEXEL) + 0.5) * NHV_TEXEL - cameraOffset.xz;
	// (and where it is on the sheet: its across and along, solved for it)
	highp vec2 a = mWorld[0].xz, b = mWorld[1].xz, r = c - mWorld[3].xz;
	highp float det = a.x * b.y - a.y * b.x;
	if (abs(det) < 1e-6)
		discard;
	highp vec2 s = vec2(r.x * b.y - r.y * b.x, a.x * r.y - a.y * r.x) / det;
	highp vec2 uv = 0.5 + vSheet.zw * s * NHV_PAD / (2.0 * vSheet.xy);
	if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
		discard;
	vec4 base = texture2D(baseTexture, uv);
	if (base.a < 0.01)
		discard;
	float clarity = clamp(fogShadingParameter
		- fogShadingParameter * length(eyeVec) / fogDistance, 0.0, 1.0);
	gl_FragColor = vec4(0.0, 0.0, 0.0, base.a * clarity);
}
