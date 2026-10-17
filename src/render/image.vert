#version 440
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=0) out vec2 texCoord;
layout(std140,binding=0) uniform Params { mat4 matrix; vec4 background; vec4 options; } params;
void main() { texCoord=uv; gl_Position=params.matrix*vec4(position,0.0,1.0); }
