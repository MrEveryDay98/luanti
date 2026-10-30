// VoxHack: a thing's shadow on the floor (content_cao.cpp: objects marked
// "nhv:shadow") - see opengl_fragment.glsl
uniform mat4 mWorld;

VARYING_ highp vec3 worldPosition;
// (the thing's picture's half size: the sheet's corners, before the matrix)
VARYING_ highp vec2 vHalf;
VARYING_ highp vec3 eyeVec;

void main(void)
{
	vHalf = abs(inVertexPosition.xy);
	worldPosition = (mWorld * inVertexPosition).xyz;
	gl_Position = mWorldViewProj * inVertexPosition;
	eyeVec = -(mWorldView * inVertexPosition).xyz;
}
