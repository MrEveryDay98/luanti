// NetHack Voxel: Quake-style sky - see opengl_fragment.glsl
VARYING_ highp vec3 vDir;

void main(void)
{
	// (the sky box is centred on the camera: a corner's position is the
	// direction you're looking in)
	vDir = inVertexPosition.xyz;
	gl_Position = mWorldViewProj * inVertexPosition;
}
