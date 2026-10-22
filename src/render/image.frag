#version 440
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform Params { mat4 matrix; vec4 background; vec4 options; } params;
layout(binding=1) uniform sampler2D sourceImage;
layout(binding=2) uniform sampler2D outputColorLut;
vec3 outputColor(vec3 rgb) {
    vec3 p=clamp(rgb,0.0,1.0)*32.0;
    float b=floor(p.b);
    vec2 lo=vec2(b*33.0+p.r+0.5,p.g+0.5)/vec2(1089.0,33.0);
    vec2 hi=vec2(min(b+1.0,32.0)*33.0+p.r+0.5,p.g+0.5)/vec2(1089.0,33.0);
    return mix(texture(outputColorLut,lo).rgb,texture(outputColorLut,hi).rgb,p.b-b);
}
void main() {
    vec4 source=texture(sourceImage,texCoord);
    if (params.options.y>0.5) source.rgb=outputColor(source.rgb);
    vec3 backdrop=params.background.rgb;
    if (params.options.x>0.5) {
        float tile=mod(floor(gl_FragCoord.x/12.0)+floor(gl_FragCoord.y/12.0),2.0);
        backdrop=vec3(mix(0.25,0.38,tile));
    }
    fragColor=vec4(mix(backdrop,source.rgb,source.a),1.0);
}
