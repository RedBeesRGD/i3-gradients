/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2015 Ingo Bürk and contributors (see also: LICENSE)
 *
 * draw.c: Utility for drawing.
 *
 */
#include "libi3.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>

#define THRESHOLD_MAP_SIZE 64
#define THRESHOLD_MAP_DIMENSION 8

/* 8x8 bayer matrix for ordered dithering. */
const double threshold_map[THRESHOLD_MAP_SIZE] = {
    0.0, 32.0, 8.0, 40.0, 2.0, 34.0, 10.0, 42.0,
    48.0, 16.0, 56.0, 24.0, 50.0, 18.0, 58.0, 26.0,
    12.0, 44.0, 4.0, 36.0, 14.0, 46.0, 6.0, 38.0,
    60.0, 28.0, 52.0, 20.0, 62.0, 30.0, 54.0, 22.0,
    3.0, 35.0, 11.0, 43.0, 1.0, 33.0, 9.0, 41.0,
    51.0, 19.0, 59.0, 27.0, 49.0, 17.0, 57.0, 25.0,
    15.0, 47.0, 7.0, 39.0, 13.0, 45.0, 5.0, 37.0,
    63.0, 31.0, 55.0, 23.0, 61.0, 29.0, 53.0, 21.0
};

/* The default visual_type to use if none is specified when creating the surface. Must be defined globally. */
extern xcb_visualtype_t *visual_type;

/* Forward declarations */
static void draw_util_set_source_color(surface_t *surface, color_t color);

static bool surface_initialized(surface_t *surface) {
    if (surface->id == XCB_NONE) {
        ELOG("Surface %p is not initialized, skipping drawing.\n", surface);
        return false;
    }
    return true;
}

/*
 * Get a GC for the given depth. The given drawable must have this depth.
 *
 * Per the X11 protocol manual for "CreateGC":
 * > The gcontext can be used with any destination drawable having the same root
 * > and depth as the specified drawable;
 */
static xcb_gcontext_t get_gc(xcb_connection_t *conn, uint8_t depth, xcb_drawable_t drawable, bool *should_free) {
    static struct {
        uint8_t depth;
        xcb_gcontext_t gc;
    } gc_cache[2] = {
        0,
    };

    size_t index = 0;
    bool cache = false;

    *should_free = false;
    for (; index < sizeof(gc_cache) / sizeof(gc_cache[0]); index++) {
        if (gc_cache[index].depth == depth) {
            return gc_cache[index].gc;
        }
        if (gc_cache[index].depth == 0) {
            cache = true;
            break;
        }
    }

    xcb_gcontext_t gc = xcb_generate_id(conn);
    /* The drawable is only used to get the root and depth, thus the GC is not
     * tied to the drawable and it can be re-used with different drawables. */
    xcb_void_cookie_t gc_cookie = xcb_create_gc_checked(conn, gc, drawable, 0, NULL);

    xcb_generic_error_t *error = xcb_request_check(conn, gc_cookie);
    if (error != NULL) {
        ELOG("Could not create graphical context. Error code: %d. Please report this bug.\n", error->error_code);
        free(error);
        return gc;
    }

    if (cache) {
        gc_cache[index].depth = depth;
        gc_cache[index].gc = gc;
    } else {
        *should_free = true;
    }

    return gc;
}

/*
 * Get depth of visual specified by visualid
 *
 */
uint16_t get_visual_depth(xcb_visualid_t visual_id) {
    xcb_depth_iterator_t depth_iter;

    depth_iter = xcb_screen_allowed_depths_iterator(root_screen);
    for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
        xcb_visualtype_iterator_t visual_iter;

        visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
        for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
            if (visual_id == visual_iter.data->visual_id) {
                return depth_iter.data->depth;
            }
        }
    }
    return 0;
}

/*
 * Initialize the surface to represent the given drawable.
 *
 */
void draw_util_surface_init(xcb_connection_t *conn, surface_t *surface, xcb_drawable_t drawable,
                            xcb_visualtype_t *visual, int width, int height) {
    surface->id = drawable;
    surface->width = width;
    surface->height = height;

    if (visual == NULL) {
        visual = visual_type;
    }

    surface->gc = get_gc(conn, get_visual_depth(visual->visual_id), drawable, &surface->owns_gc);
    surface->surface = cairo_xcb_surface_create(conn, surface->id, visual, width, height);
    surface->cr = cairo_create(surface->surface);
}

/*
 * Destroys the surface.
 *
 */
void draw_util_surface_free(xcb_connection_t *conn, surface_t *surface) {
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
    if (surface->cr) {
        status = cairo_status(surface->cr);
    }
    if (status != CAIRO_STATUS_SUCCESS) {
        LOG("Found cairo context in an error status while freeing, error %d is %s",
            status, cairo_status_to_string(status));
    }

    if (surface->owns_gc) {
        xcb_free_gc(conn, surface->gc);
    }
    cairo_surface_destroy(surface->surface);
    cairo_destroy(surface->cr);

    /* We need to explicitly set these to NULL to avoid assertion errors in
     * cairo when calling this multiple times. This can happen, for example,
     * when setting the border of a window to none and then closing it. */
    surface->surface = NULL;
    surface->cr = NULL;
}

/*
 * Resize the surface to the given size.
 *
 */
void draw_util_surface_set_size(surface_t *surface, int width, int height) {
    surface->width = width;
    surface->height = height;
    cairo_xcb_surface_set_size(surface->surface, width, height);
}

/*
 * Parses the given color in hex format to an internal color representation.
 * Note that the input must begin with a hash sign, e.g., "#3fbc59".
 *
 */
color_t draw_util_hex_to_color(const char *color) {
    if (strlen(color) < 6 || color[0] != '#') {
        ELOG("Could not parse color: %s\n", color);
        return draw_util_hex_to_color("#A9A9A9");
    }

    char alpha[2];
    if (strlen(color) == strlen("#rrggbbaa")) {
        alpha[0] = color[7];
        alpha[1] = color[8];
    } else {
        alpha[0] = alpha[1] = 'F';
    }

    char groups[4][3] = {
        {color[1], color[2], '\0'},
        {color[3], color[4], '\0'},
        {color[5], color[6], '\0'},
        {alpha[0], alpha[1], '\0'}};

    return (color_t){
        .red = strtol(groups[0], NULL, 16) / 255.0,
        .green = strtol(groups[1], NULL, 16) / 255.0,
        .blue = strtol(groups[2], NULL, 16) / 255.0,
        .alpha = strtol(groups[3], NULL, 16) / 255.0,
        .colorpixel = get_colorpixel(color)};
}

/*
 * Set the given color as the source color on the surface.
 *
 */
static void draw_util_set_source_color(surface_t *surface, color_t color) {
    if (!surface_initialized(surface)) {
        return;
    }

    cairo_set_source_rgba(surface->cr, color.red, color.green, color.blue, color.alpha);
}

/*
 * Draw the given text using libi3.
 * This function also marks the surface dirty which is needed if other means of
 * drawing are used. This will be the case when using XCB to draw text.
 *
 */
void draw_util_text(i3String *text, surface_t *surface, color_t fg_color, color_t bg_color, int x, int y, int max_width) {
    if (!surface_initialized(surface)) {
        return;
    }

    /* Flush any changes before we draw the text as this might use XCB directly. */
    CAIRO_SURFACE_FLUSH(surface->surface);

    set_font_colors(surface->gc, fg_color, bg_color);
    draw_text(text, surface->id, surface->gc, surface->surface, x, y, max_width);

    /* Notify cairo that we (possibly) used another way to draw on the surface. */
    cairo_surface_mark_dirty(surface->surface);
}

/**
 * Draw the given image using libi3.
 * This function is a convenience wrapper and takes care of flushing the
 * surface as well as restoring the cairo state.
 *
 */
void draw_util_image(cairo_surface_t *image, surface_t *surface, int x, int y, int width, int height) {
    if (!surface_initialized(surface)) {
        return;
    }

    cairo_save(surface->cr);

    cairo_translate(surface->cr, x, y);

    const int src_width = cairo_image_surface_get_width(image);
    const int src_height = cairo_image_surface_get_height(image);
    double scale = MIN((double)width / src_width, (double)height / src_height);
    cairo_scale(surface->cr, scale, scale);

    cairo_set_source_surface(surface->cr, image, 0, 0);
    cairo_paint(surface->cr);

    cairo_restore(surface->cr);
}

/*
 * Draws a filled rectangle.
 * This function is a convenience wrapper and takes care of flushing the
 * surface as well as restoring the cairo state.
 *
 */

void draw_util_rectangle(surface_t *surface, color_t color, double x, double y, double w, double h) {
    if (!surface_initialized(surface)) {
        return;
    }

    cairo_save(surface->cr);

    /* Using the SOURCE operator will copy both color and alpha information directly
     * onto the surface rather than blending it. This is a bit more efficient and
     * allows better color control for the user when using opacity. */
    cairo_set_operator(surface->cr, CAIRO_OPERATOR_SOURCE);
    draw_util_set_source_color(surface, color);

    cairo_rectangle(surface->cr, x, y, w, h);
    cairo_fill(surface->cr);

    /* Make sure we flush the surface for any text drawing operations that could follow.
     * Since we support drawing text via XCB, we need this. */
    CAIRO_SURFACE_FLUSH(surface->surface);

    cairo_restore(surface->cr);
}

// TODO: put this in a better place! (it might also be redundant, but too lazy to check)
double clamp_double(double n, double a, double b) {
    if (n < a) return a;
    if (n > b) return b;
    return n;
}

// TODO: put this in a better place! (it might also be redundant, but too lazy to check)
double lerp_double(double a, double b, double t)
{
    return a + (b - a) * t;
}

void draw_util_rectangle_gradient(surface_t *surface, color_t startColor, color_t endColor, double x, double y, double w, double h, bool use_dithering, double noise_gain) {
    // feature ideas:
    // - control offset?

    // TODO: implement reading these vars in the config! this is temporary!
    const int num_colors = 256;
    const int N = num_colors - 1;

    if (!surface_initialized(surface)) {
        return;
    }

    if (use_dithering) {
        int width = floor(w) + 1;
        int height = floor(h) + 1;
        int stride = width * sizeof(int);

        unsigned int *pixels = malloc(width * height * sizeof(int));

        if (pixels == NULL) {
            // fallback: draw gradients if we can't dither for some reason
            goto draw_nondithered;
        }        

        double r0 = startColor.red, g0 = startColor.green, b0 = startColor.blue;
        double r1 = endColor.red, g1 = endColor.green, b1 = endColor.blue;

        for(int j = 0; j < height; ++j) {                        
            for(int i = 0; i < width; ++i) {
                double t = (double)i / (double)width;            

                double r = lerp_double(r0, r1, t);
                double g = lerp_double(g0, g1, t);
                double b = lerp_double(b0, b1, t);

                // color quantization
                double r_q = floor(r * (double)N + 0.5) / (double)N;
                double g_q = floor(g * (double)N + 0.5) / (double)N;
                double b_q = floor(b * (double)N + 0.5) / (double)N;
                
                int s_x = i % THRESHOLD_MAP_DIMENSION;
                int s_y = j % THRESHOLD_MAP_DIMENSION;

                double m_s = threshold_map[s_y * THRESHOLD_MAP_DIMENSION + s_x];

                double noise = (m_s / (double)(THRESHOLD_MAP_SIZE)) - 0.5;

                r_q = clamp_double(r_q + noise * noise_gain, 0.0, 1.0);
                g_q = clamp_double(g_q + noise * noise_gain, 0.0, 1.0);
                b_q = clamp_double(b_q + noise * noise_gain, 0.0, 1.0);

                unsigned char r_c = (unsigned char)(floor(r_q * 255.0));
                unsigned char g_c = (unsigned char)(floor(g_q * 255.0));
                unsigned char b_c = (unsigned char)(floor(b_q * 255.0));

                // pixel format is ARGB32
                unsigned int pixel = 0xFF000000;
                pixel |= ((unsigned int)r_c) << 16;
                pixel |= ((unsigned int)g_c) << 8;
                pixel |= ((unsigned int)b_c);

                pixels[j * width + i] = pixel;
            }
        }

        cairo_surface_t *image_surface = cairo_image_surface_create_for_data((unsigned char*)pixels,
                                                                             CAIRO_FORMAT_ARGB32,
                                                                             width,
                                                                             height,
                                                                             stride);

        if (cairo_surface_status(image_surface) != CAIRO_STATUS_SUCCESS) {
            if (pixels) free(pixels);
            // fallback: draw gradients if we can't dither for some reason
            goto draw_nondithered;
        }

        cairo_save(surface->cr);
        cairo_set_operator(surface->cr, CAIRO_OPERATOR_SOURCE);

        cairo_set_source_surface(surface->cr, image_surface, x, y);

        cairo_paint(surface->cr);

        CAIRO_SURFACE_FLUSH(surface->surface);
        cairo_restore(surface->cr);

        cairo_surface_destroy(image_surface);
        free(pixels);
    }

    else {
    draw_nondithered:
        cairo_save(surface->cr);

        cairo_set_operator(surface->cr, CAIRO_OPERATOR_SOURCE);

        // Create a linear gradient from top-left to bottom-right of the rectangle
        cairo_pattern_t *pattern = cairo_pattern_create_linear(x, y, x + w, y + h);

        cairo_pattern_add_color_stop_rgba(pattern, 0.0, startColor.red, startColor.green, startColor.blue, startColor.alpha); 
        cairo_pattern_add_color_stop_rgba(pattern, 1.0, endColor.red, endColor.green, endColor.blue, endColor.alpha); 

        cairo_set_source(surface->cr, pattern);
        cairo_rectangle(surface->cr, x, y, w, h);
        cairo_fill(surface->cr);

        CAIRO_SURFACE_FLUSH(surface->surface);

        cairo_pattern_destroy(pattern);       
        cairo_restore(surface->cr);
    }
}
/*
 * Clears a surface with the given color.
 *
 */
void draw_util_clear_surface(surface_t *surface, color_t color) {
    if (!surface_initialized(surface)) {
        return;
    }

    cairo_save(surface->cr);

    /* Using the SOURCE operator will copy both color and alpha information directly
     * onto the surface rather than blending it. This is a bit more efficient and
     * allows better color control for the user when using opacity. */
    cairo_set_operator(surface->cr, CAIRO_OPERATOR_SOURCE);
    draw_util_set_source_color(surface, color);

    cairo_paint(surface->cr);

    /* Make sure we flush the surface for any text drawing operations that could follow.
     * Since we support drawing text via XCB, we need this. */
    CAIRO_SURFACE_FLUSH(surface->surface);

    cairo_restore(surface->cr);
}

/*
 * Copies a surface onto another surface.
 *
 */
void draw_util_copy_surface(surface_t *src, surface_t *dest, double src_x, double src_y,
                            double dest_x, double dest_y, double width, double height) {
    if (!surface_initialized(src) ||
        !surface_initialized(dest)) {
        return;
    }

    cairo_save(dest->cr);

    /* Using the SOURCE operator will copy both color and alpha information directly
     * onto the surface rather than blending it. This is a bit more efficient and
     * allows better color control for the user when using opacity. */
    cairo_set_operator(dest->cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(dest->cr, src->surface, dest_x - src_x, dest_y - src_y);

    cairo_rectangle(dest->cr, dest_x, dest_y, width, height);
    cairo_fill(dest->cr);

    /* Make sure we flush the surface for any text drawing operations that could follow.
     * Since we support drawing text via XCB, we need this. */
    CAIRO_SURFACE_FLUSH(src->surface);
    CAIRO_SURFACE_FLUSH(dest->surface);

    cairo_restore(dest->cr);
}
