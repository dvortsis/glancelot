#version 440
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform Params { mat4 matrix; vec4 background; vec4 options; } params;
layout(binding=1) uniform sampler2D sourceImage;
void main() {
    vec4 source=texture(sourceImage,texCoord);
    vec3 backdrop=params.background.rgb;
    if (params.options.x>0.5) {
        float tile=mod(floor(gl_FragCoord.x/12.0)+floor(gl_FragCoord.y/12.0),2.0);
        backdrop=vec3(mix(0.25,0.38,tile));
    }
    fragColor=vec4(mix(backdrop,source.rgb,source.a),1.0);
}
