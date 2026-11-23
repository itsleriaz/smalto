#version 440

layout(location = 0) in vec2 v_screen;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    vec4 viewport;   // xy: target size, zw: sprite size
    vec4 xform;      // xy: offset, z: zoom, w: NDC y sign
    vec4 background;
    vec4 checkA;
    vec4 checkB;
    vec4 grid;       // the pixel grid's color (alpha: its strength); alpha 0: automatic
    vec4 hoverColor;
    vec4 hoverRect;
    vec4 params;     // x: checker size, y: grid strength, z: ants phase, w: device pixel ratio
    vec4 selection;  // bounds: x, y, w, h in canvas pixels; w = 0: none
    vec4 symmetry;   // xy: where the axes cross (canvas pixels); z: axes shown, bits: 1 vertical, 2 horizontal, 4 rising, 8 falling diagonal
    vec4 customGrid; // x, y: offset, zw: cell size (canvas pixels); w = 0: off
    vec4 brushStyle; // x: 1 = the brush texture shows its own colors (at opacity y); z: outline strength
    vec4 border;     // the line around the canvas; alpha blends it
    vec4 tiling;     // x / y: 1 = the sprite repeats across that axis; z: 1 = the checkerboard is fixed on screen, w: its cell (px)
    vec4 docGrid;    // the document grid's color; alpha 0: automatic
    vec4 tileDim;    // x: how far the tiled view's copies fade toward the background; y: 1 = the onion skin is drawn over the sprite
};

layout(binding = 1) uniform sampler2D sprite;
layout(binding = 2) uniform sampler2D overlay; // tool previews, drawn over the sprite
layout(binding = 3) uniform sampler2D onion;   // onion skin, drawn behind the sprite (over it with tileDim.y)
layout(binding = 4) uniform sampler2D selectionMask; // r > 0.5: selected
layout(binding = 5) uniform sampler2D brushMask;     // the brush over hoverRect: footprint (alpha) or colors

// Black over light colors, white over dark ones: lines that show on anything.
vec3 contrast(vec3 c)
{
    return vec3(1.0 - step(0.6, dot(c, vec3(0.299, 0.587, 0.114))));
}

// Whether canvas pixel `q` is in the brush footprint shown over hoverRect.
bool inBrush(vec2 q)
{
    if (q.x < hoverRect.x || q.y < hoverRect.y || q.x >= hoverRect.x + hoverRect.z || q.y >= hoverRect.y + hoverRect.w)
        return false;
    float a = texture(brushMask, (q - hoverRect.xy + 0.5) / hoverRect.zw).a;
    return brushStyle.x > 0.5 ? a > 0.0 : a > 0.5;
}

// Whether canvas pixel `px` (whole numbers) is selected; outside the canvas never.
bool selectedAt(vec2 px, vec2 size)
{
    if (px.x < 0.0 || px.y < 0.0 || px.x >= size.x || px.y >= size.y)
        return false;
    return texture(selectionMask, (px + 0.5) / size).r > 0.5;
}

void main()
{
    vec2 size = viewport.zw;
    float zoom = xform.z;
    vec2 p = (v_screen - xform.xy) / zoom; // canvas pixel coordinates
    // Tiled view: the copies show the sprite's pixels (and its overlays).
    bool copy = false;
    if (tiling.x > 0.5) {
        copy = copy || p.x < 0.0 || p.x >= size.x;
        p.x -= floor(p.x / size.x) * size.x;
    }
    if (tiling.y > 0.5) {
        copy = copy || p.y < 0.0 || p.y >= size.y;
        p.y -= floor(p.y / size.y) * size.y;
    }

    bool inside = p.x >= 0.0 && p.y >= 0.0 && p.x < size.x && p.y < size.y;
    if (!inside) {
        // A 1 px line (in screen pixels) around the canvas.
        vec2 d = max(max(-p, p - size), 0.0) * zoom;
        bool line = max(d.x, d.y) <= params.w;
        fragColor = vec4(line ? mix(background.rgb, border.rgb, border.a) : background.rgb, 1.0);
        return;
    }

    vec2 cell = tiling.z > 0.5 ? floor(v_screen / tiling.w) : floor(p / params.x);
    vec3 col = mix(checkA.rgb, checkB.rgb, mod(cell.x + cell.y, 2.0));

    vec4 n = texture(onion, p / size);
    vec4 s = texture(sprite, p / size);
    if (tileDim.y > 0.5) {
        col = mix(col, s.rgb, s.a);
        col = mix(col, n.rgb, n.a);
    } else {
        col = mix(col, n.rgb, n.a);
        col = mix(col, s.rgb, s.a);
    }
    vec4 o = texture(overlay, p / size);
    col = mix(col, o.rgb, o.a);

    vec2 px = floor(p);
    if (hoverColor.a > 0.0 && px.x >= hoverRect.x && px.y >= hoverRect.y
            && px.x < hoverRect.x + hoverRect.z && px.y < hoverRect.y + hoverRect.w) {
        vec4 b = texture(brushMask, (px - hoverRect.xy + 0.5) / hoverRect.zw);
        if (brushStyle.x > 0.5)
            col = mix(col, b.rgb, b.a * brushStyle.y);
        else if (b.a > 0.5)
            col = mix(col, hoverColor.rgb, hoverColor.a);
        // An outline just inside the footprint's edge, one screen pixel wide
        // (never more than half a canvas pixel), so it shows over any paint.
        if (brushStyle.z > 0.0 && inBrush(px)) {
            float w = min(max(1.0, params.w), 0.5 * zoom);
            vec2 a = fract(p) * zoom;         // screen px from the pixel's left / top edge
            vec2 c = (1.0 - fract(p)) * zoom; // and from its right / bottom edge
            bool edge = (a.x < w && !inBrush(px + vec2(-1.0, 0.0)))
                     || (c.x <= w && !inBrush(px + vec2(1.0, 0.0)))
                     || (a.y < w && !inBrush(px + vec2(0.0, -1.0)))
                     || (c.y <= w && !inBrush(px + vec2(0.0, 1.0)));
            if (edge)
                col = mix(col, contrast(col), brushStyle.z);
        }
    }

    // Symmetry axes: thin dashed lines across the canvas.
    {
        float w = max(1.0, params.w);
        float dash = mod(floor((v_screen.x + v_screen.y) / (6.0 * w)), 2.0);
        // Half-open, so exactly one screen pixel column / row: when the axis
        // falls on a pixel boundary, both neighbours are half a pixel away.
        vec2 d = (p - symmetry.xy) * zoom;
        int axes = int(symmetry.z + 0.5);
        bool on = ((axes & 1) != 0 && d.x >= -0.5 * w && d.x < 0.5 * w)
               || ((axes & 2) != 0 && d.y >= -0.5 * w && d.y < 0.5 * w)
               // Diagonals: distance to the line, about a screen pixel wide.
               || ((axes & 4) != 0 && abs(d.x + d.y) < 0.75 * w)
               || ((axes & 8) != 0 && abs(d.x - d.y) < 0.75 * w);
        if (on)
            col = mix(col, vec3(0.43, 0.55, 1.0), 0.35 + 0.45 * dash);
    }

    if (params.y > 0.0) {
        vec2 f = fract(p) * zoom; // distance in screen px from the pixel's top-left edge
        float line = (f.x < 1.0 || f.y < 1.0) ? 1.0 : 0.0;
        if (grid.a > 0.0)
            col = mix(col, grid.rgb, line * grid.a * params.y);
        else
            col = mix(col, contrast(col), line * 0.3 * params.y);
    }

    // The document's grid: one screen pixel line on each cell edge, white over
    // dark backgrounds and black over light ones (by the color beneath).
    if (customGrid.z > 0.0) {
        vec2 q = p - customGrid.xy;
        vec2 f = (q - floor(q / customGrid.zw) * customGrid.zw) * zoom; // screen px into the cell
        float w = max(1.0, params.w);
        if (f.x < w || f.y < w) {
            if (docGrid.a > 0.0)
                col = mix(col, docGrid.rgb, docGrid.a);
            else
                col = mix(col, contrast(col), 0.6);
        }
    }

    // Selection: marching ants just inside every edge between a selected and
    // an unselected pixel (the mask can have any shape, holes included).
    if (selection.z > 0.0 && px.x >= selection.x && px.y >= selection.y
            && px.x < selection.x + selection.z && px.y < selection.y + selection.w
            && selectedAt(px, size)) {
        vec2 s = v_screen;
        float w = max(1.0, params.w);
        vec2 near = fract(p) * zoom;        // screen px from the pixel's top-left edge
        vec2 far = zoom - near;             // ... and from its bottom-right edge
        bool edge = (near.x < w && !selectedAt(px - vec2(1.0, 0.0), size))
                 || (far.x <= w && !selectedAt(px + vec2(1.0, 0.0), size))
                 || (near.y < w && !selectedAt(px - vec2(0.0, 1.0), size))
                 || (far.y <= w && !selectedAt(px + vec2(0.0, 1.0), size));
        if (edge) {
            float dash = mod(floor((s.x + s.y + params.z * w) / (4.0 * w)), 2.0);
            col = mix(vec3(0.0), vec3(1.0), dash);
        }
    }

    if (copy)
        col = mix(col, background.rgb, tileDim.x);
    fragColor = vec4(col, 1.0);
}
