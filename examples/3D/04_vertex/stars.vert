
#ifdef GL_ES
precision mediump float;
#endif

uniform mat4        u_modelMatrix;
uniform mat4        u_viewMatrix;
uniform mat4        u_projectionMatrix;
uniform mat4        u_modelViewProjectionMatrix;
uniform float       u_time;

attribute vec4      a_position;
varying vec4        v_position;

varying vec4        v_color;
varying vec2        v_texcoord;

void main(void) {
    v_position = a_position;
    v_texcoord = a_position.xy;

    float scale = 100.0;
    float speed = u_time * 0.01;

    v_position.z += speed;
    // v_position.y += sin(u_time * 0.14) * v_position.z * 0.025;
    // v_position.x += cos(u_time * 0.13) * v_position.y * 0.025;

    v_position.xyz = fract(v_position.xyz);
    v_position.xyz = (v_position.xyz - 0.5) * scale;

    vec2 st = v_texcoord;
    float brightness = 1.0-length(v_position.xyz)/scale;
    brightness = pow(brightness, 10.0);
    v_color = vec4(vec3(st, a_position.z), brightness);

    gl_PointSize = 5.0 * brightness;
    
    gl_Position = u_projectionMatrix * u_viewMatrix * v_position;
}
