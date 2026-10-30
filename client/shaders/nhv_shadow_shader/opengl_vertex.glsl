// VoxHack: a thing's shadow on the floor (content_cao.cpp: objects marked
// "nhv:shadow") - see opengl_fragment.glsl
uniform mat4 mWorld;

VARYING_ highp vec3 worldPosition;
// (the sheet's half size and which way its picture runs across and down:
// the same at every corner)
VARYING_ highp vec4 vSheet;
VARYING_ highp vec3 eyeVec;

void main(void)
{
	highp vec2 at = inVertexPosition.xy;
	highp vec2 uv = inTexCoord0.xy;
	vSheet = vec4(abs(at), sign((uv - 0.5) * at));
	worldPosition = (mWorld * inVertexPosition).xyz;
	gl_Position = mWorldViewProj * inVertexPosition;
	eyeVec = -(mWorldView * inVertexPosition).xyz;
}
