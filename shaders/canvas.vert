#version 440

layout(location = 0) in vec2 position;
layout(location = 0) out vec2 v_screen;

layout(std140, binding = 0) uniform buf {
    vec4 viewport;
    vec4 xform;
    vec4 background;
    vec4 checkA;
    vec4 checkB;
    vec4 grid;
    vec4 hoverColor;
    vec4 hoverRect;
    vec4 params;
    vec4 selection;
    vec4 symmetry;   // xy: where the axes cross (canvas pixels); z: axes shown, bits: 1 vertical, 2 horizontal, 4 rising, 8 falling diagonal
    vec4 customGrid;
    vec4 brushStyle; // x: 1 = the brush texture shows its own colors (at opacity y)
    vec4 border;     // the line around the canvas; alpha blends it
    vec4 tiling;     // x / y: 1 = the sprite repeats across that axis
};

void main()
{
    // Screen position in pixels, origin top-left.
    v_screen = vec2(position.x, 1.0 - position.y) * viewport.xy;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
    gl_Position.y *= xform.w;
}
