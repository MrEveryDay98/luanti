// NetHack Voxel: Quake-style sky clouds - see opengl_fragment.glsl
VARYING_ highp vec3 vPos;

void main(void)
{
	// (a flat plane overhead, centred on the camera)
	vPos = inVertexPosition.xyz;
	gl_Position = mWorldViewProj * inVertexPosition;
}
