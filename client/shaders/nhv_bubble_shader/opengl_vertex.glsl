// NetHack Voxel: the Plane of Water's bubbles on the floor - see
// opengl_fragment.glsl
VARYING_ highp vec2 vWorld;
VARYING_ highp vec3 eyeVec;

void main(void)
{
	// (the sheet's corners carry their places in the world, in nodes)
	vWorld = inTexCoord0.xy;
	gl_Position = mWorldViewProj * inVertexPosition;
	eyeVec = -(mWorldView * inVertexPosition).xyz;
}
