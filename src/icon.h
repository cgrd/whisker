// SPDX-License-Identifier: MIT
#pragma once
#include <cairo/cairo.h>
#include <cmath>

inline void drawWhiskerIcon(cairo_t* cr, double cx, double cy, double size) {
    double s = size / 512.0;
    double ox = cx - size / 2.0;
    double oy = cy - size / 2.0;

    cairo_save(cr);

    // Pedal platform
    double pr = 24 * s;
    double px = ox + 126 * s, py = oy + 388 * s;
    double pw = 260 * s, ph = 48 * s;
    cairo_new_sub_path(cr);
    cairo_arc(cr, px + pw - pr, py + pr, pr, -M_PI / 2, 0);
    cairo_arc(cr, px + pw - pr, py + ph - pr, pr, 0, M_PI / 2);
    cairo_arc(cr, px + pr, py + ph - pr, pr, M_PI / 2, M_PI);
    cairo_arc(cr, px + pr, py + pr, pr, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
    cairo_fill(cr);

    // Palm pad
    cairo_save(cr);
    cairo_translate(cr, ox + 256 * s, oy + 304 * s);
    cairo_scale(cr, 72 * s, 54 * s);
    cairo_arc(cr, 0, 0, 1.0, 0, 2 * M_PI);
    cairo_restore(cr);
    cairo_fill(cr);

    // Toe beans
    cairo_arc(cr, ox + 184 * s, oy + 240 * s, 28 * s, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_arc(cr, ox + 256 * s, oy + 218 * s, 28 * s, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_arc(cr, ox + 328 * s, oy + 240 * s, 28 * s, 0, 2 * M_PI);
    cairo_fill(cr);

    // Whiskers
    cairo_set_line_width(cr, 11 * s);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    // Left
    cairo_move_to(cr, ox + 156 * s, oy + 278 * s);
    cairo_line_to(cr, ox + 68 * s, oy + 254 * s);
    cairo_stroke(cr);
    cairo_move_to(cr, ox + 152 * s, oy + 302 * s);
    cairo_line_to(cr, ox + 60 * s, oy + 302 * s);
    cairo_stroke(cr);
    cairo_move_to(cr, ox + 156 * s, oy + 326 * s);
    cairo_line_to(cr, ox + 68 * s, oy + 350 * s);
    cairo_stroke(cr);

    // Right
    cairo_move_to(cr, ox + 356 * s, oy + 278 * s);
    cairo_line_to(cr, ox + 444 * s, oy + 254 * s);
    cairo_stroke(cr);
    cairo_move_to(cr, ox + 360 * s, oy + 302 * s);
    cairo_line_to(cr, ox + 452 * s, oy + 302 * s);
    cairo_stroke(cr);
    cairo_move_to(cr, ox + 356 * s, oy + 326 * s);
    cairo_line_to(cr, ox + 444 * s, oy + 350 * s);
    cairo_stroke(cr);

    cairo_restore(cr);
}
