#version 130            // --*-c-*--

//in vec4 fragmentColor;
in vec2 fragmentUV;
in float fragmentAmbient;

uniform sampler2D theTextureSampler;

void main(void) {
  //gl_FragColor = vec4(fragmentColor,1) + vec4(0.2, 0.2, 0.2, 0);
  //gl_FragColor = fragmentColor;
  //gl_FragColor = vec4(0.666,0.666,0.666,1) + 0.333*texture(theTextureSampler, fragmentUV);
  gl_FragColor = texture(theTextureSampler, fragmentUV);
  //gl_FragColor = mix( vec4(1,1,1,1),
  //texture(theTextureSampler, fragmentUV) * fragmentAmbient,
  //fragmentFog );
  //  if (gl_FragColor.a < 0.5) {
  //discard;
  //}
}
