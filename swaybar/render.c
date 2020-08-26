#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <linux/input-event-codes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "cairo.h"
#include "pango.h"
#include "pool-buffer.h"
#include "swaybar/bar.h"
#include "swaybar/config.h"
#include "swaybar/i3bar.h"
#include "swaybar/ipc.h"
#include "swaybar/render.h"
#include "swaybar/status_line.h"
#include "swaybar/system_info.h"
#include "swaybar/badges.h"
#if HAVE_TRAY
#include "swaybar/tray/tray.h"
#endif
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static const int WS_HORIZONTAL_PADDING = 5;
static const double WS_VERTICAL_PADDING = 1.5;
static const double BORDER_WIDTH = 1;

struct badge_t {
	char const* text;
	uint32_t bg, border, text_color;
	float x_offset;
};

#define M_PI (3.1415926f)

static uint32_t render_status_badge(cairo_t *cairo,
		struct swaybar_output *output, double *x,
		struct badge_t *badge) {
	struct swaybar_config *config = output->bar->config;

	// Calculate text size
	int text_width, text_height;
	get_text_size(cairo, config->font, &text_width, &text_height, NULL,
			output->scale, config->pango_markup, "%s", badge->text);

	// Draw badge
	double margin = 4 * output->scale;
	double padding = 6 * output->scale;
	double shear_offset = 4 * output->scale;

	double badge_width = text_width + 2 * padding;
	double badge_height = text_height * 1.1f;
	double x_offset = badge->x_offset * badge_width;

	double clip_right_x = *x + shear_offset;
	double margin_right_x = *x + x_offset;
	double padding_right_x = margin_right_x - margin;
	double text_x = padding_right_x - padding - text_width;
	double padding_left_x = text_x - padding;
	double margin_left_x = padding_left_x - margin;
	double clip_left_x = margin_left_x;

	double top_start_x = padding_left_x + shear_offset;
	double top_end_x = padding_right_x + shear_offset;
	double bot_start_x = padding_left_x;
	double bot_end_x = padding_right_x;

	*x = margin_left_x;

	cairo_save(cairo);
	cairo_rectangle(cairo, clip_left_x, 0, clip_right_x - clip_left_x, badge_height);
	cairo_clip(cairo);

	cairo_new_path(cairo);
	cairo_move_to(cairo, top_start_x, 0);
	cairo_line_to(cairo, top_end_x, 0);
	cairo_line_to(cairo, bot_end_x, badge_height);
	cairo_line_to(cairo, bot_start_x, badge_height);
	cairo_line_to(cairo, top_start_x, 0);
	cairo_set_source_u32(cairo, badge->bg);
	cairo_fill_preserve(cairo);
	cairo_set_source_u32(cairo, badge->border);
	cairo_stroke(cairo);

	// Draw text
	double ws_vertical_padding = config->status_padding * output->scale;

	uint32_t ideal_height = text_height + ws_vertical_padding * 2;
	uint32_t ideal_surface_height = ideal_height / output->scale;
	if (!output->bar->config->height &&
			output->height < ideal_surface_height) {
		return ideal_surface_height;
	}

	cairo_set_source_u32(cairo, badge->text_color);

	uint32_t height = output->height * output->scale;
	double text_y = height / 2.0 - text_height / 2.0;
	cairo_move_to(cairo, text_x, (int)floor(text_y));
	pango_printf(cairo, config->font, output->scale,
			config->pango_markup, "%s", badge->text);

	cairo_restore(cairo);

	return badge_height;
}

static uint32_t render_badges(cairo_t *cairo,
		struct swaybar_output *output, double *x) {
	uint32_t ret = 0;
	uint32_t res;

	const char *text;
	uint32_t col_bg, col_border, col_text;
	float x_offset;

	struct badges_t* B = output->bar->badges;

	int badge_count = get_badges_count(B);
	for(int i = 0; i < badge_count; i++) {
		text = get_badge_text(B, i);
		if(text == NULL) {
			continue;
		}

		if(!get_badge_colors(B, i, &col_bg, &col_border, &col_text)) {
			continue;
		}

		x_offset = get_badge_x_offset(B, i);
		if(x_offset >= 1.0) {
			continue;
		}

		struct badge_t badge;
		badge.text = text;
		badge.bg = col_bg;
		badge.border = col_border;
		badge.text_color = col_text;
		badge.x_offset = x_offset;
		res = render_status_badge(cairo, output, x, &badge);

		if(res > ret) {
			ret = res;
		}
	}

	return ret;
}

static uint32_t render_binding_mode_indicator(cairo_t *cairo,
		struct swaybar_output *output, double x) {
	const char *mode = output->bar->mode;
	if (!mode) {
		return 0;
	}

	struct swaybar_config *config = output->bar->config;
	int text_width, text_height;
	get_text_size(cairo, config->font, &text_width, &text_height, NULL,
			output->scale, output->bar->mode_pango_markup,
			"%s", mode);

	int ws_vertical_padding = WS_VERTICAL_PADDING * output->scale;
	int ws_horizontal_padding = WS_HORIZONTAL_PADDING * output->scale;
	int border_width = BORDER_WIDTH * output->scale;

	uint32_t ideal_height = text_height + ws_vertical_padding * 2
		+ border_width * 2;
	uint32_t ideal_surface_height = ideal_height / output->scale;
	if (!output->bar->config->height &&
			output->height < ideal_surface_height) {
		return ideal_surface_height;
	}
	uint32_t width = text_width + ws_horizontal_padding * 2 + border_width * 2;

	uint32_t height = output->height * output->scale;
	cairo_set_source_u32(cairo, config->colors.binding_mode.background);
	cairo_rectangle(cairo, x, 0, width, height);
	cairo_fill(cairo);

	cairo_set_source_u32(cairo, config->colors.binding_mode.border);
	cairo_rectangle(cairo, x, 0, width, border_width);
	cairo_fill(cairo);
	cairo_rectangle(cairo, x, 0, border_width, height);
	cairo_fill(cairo);
	cairo_rectangle(cairo, x + width - border_width, 0, border_width, height);
	cairo_fill(cairo);
	cairo_rectangle(cairo, x, height - border_width, width, border_width);
	cairo_fill(cairo);

	double text_y = height / 2.0 - text_height / 2.0;
	cairo_set_source_u32(cairo, config->colors.binding_mode.text);
	cairo_move_to(cairo, x + width / 2 - text_width / 2, (int)floor(text_y));
	pango_printf(cairo, config->font, output->scale,
			output->bar->mode_pango_markup, "%s", mode);
	return output->height;
}

static enum hotspot_event_handling workspace_hotspot_callback(
		struct swaybar_output *output, struct swaybar_hotspot *hotspot,
		double x, double y, uint32_t button, void *data) {
	if (button != BTN_LEFT) {
		return HOTSPOT_PROCESS;
	}
	ipc_send_workspace_command(output->bar, (const char *)data);
	return HOTSPOT_IGNORE;
}

static uint32_t render_workspace_button(cairo_t *cairo,
		struct swaybar_output *output,
		struct swaybar_workspace *ws, double *x) {
	struct swaybar_config *config = output->bar->config;
	struct box_colors box_colors;
	if (ws->urgent) {
		box_colors = config->colors.urgent_workspace;
	} else if (ws->focused) {
		box_colors = config->colors.focused_workspace;
	} else if (ws->visible) {
		box_colors = config->colors.active_workspace;
	} else {
		box_colors = config->colors.inactive_workspace;
	}

	uint32_t height = output->height * output->scale;

	int text_width, text_height;
	get_text_size(cairo, config->font, &text_width, &text_height, NULL,
			output->scale, config->pango_markup, "%s", ws->label);

	int ws_vertical_padding = WS_VERTICAL_PADDING * output->scale;
	int ws_horizontal_padding = WS_HORIZONTAL_PADDING * output->scale;
	int border_width = BORDER_WIDTH * output->scale;

	uint32_t ideal_height = ws_vertical_padding * 2 + text_height
		+ border_width * 2;
	uint32_t ideal_surface_height = ideal_height / output->scale;
	if (!output->bar->config->height &&
			output->height < ideal_surface_height) {
		return ideal_surface_height;
	}

	uint32_t padding = 2 * output->scale;
	uint32_t width = ws_horizontal_padding * 2 + text_width + border_width * 2 +
		padding * 2;

	double shear_offset = 4 * output->scale;
	double top_start_x = *x + shear_offset;
	double bot_start_x = *x;
	double top_end_x = top_start_x + width;
	double bot_end_x = bot_start_x + width;

	cairo_set_source_u32(cairo, box_colors.background);
	cairo_move_to(cairo, top_start_x, 0);
	cairo_line_to(cairo, top_end_x, 0);
	cairo_line_to(cairo, bot_end_x, height);
	cairo_line_to(cairo, bot_start_x, height);
	cairo_line_to(cairo, top_start_x, 0);
	cairo_fill_preserve(cairo);
	cairo_set_source_u32(cairo, box_colors.border);
	cairo_stroke(cairo);

	double text_y = height / 2.0 - text_height / 2.0;
	cairo_set_source_u32(cairo, box_colors.text);
	cairo_move_to(cairo,
			top_start_x - padding / 2 + width / 2 - text_width / 2, (int)floor(text_y));
	pango_printf(cairo, config->font, output->scale, config->pango_markup,
			"%s", ws->label);

	struct swaybar_hotspot *hotspot = calloc(1, sizeof(struct swaybar_hotspot));
	hotspot->x = *x;
	hotspot->y = 0;
	hotspot->width = width;
	hotspot->height = height;
	hotspot->callback = workspace_hotspot_callback;
	hotspot->destroy = free;
	hotspot->data = strdup(ws->name);
	wl_list_insert(&output->hotspots, &hotspot->link);

	uint32_t margin = 4 * output->scale;
	*x += width + margin;
	return output->height;
}

static uint32_t render_to_cairo(cairo_t *cairo, struct swaybar_output *output) {
	struct swaybar *bar = output->bar;
	struct swaybar_config *config = bar->config;

	int th;
	get_text_size(cairo, config->font, NULL, &th, NULL, output->scale, false, "");
	uint32_t max_height = (th + WS_VERTICAL_PADDING * 4) / output->scale;
	/*
	 * Each render_* function takes the actual height of the bar, and returns
	 * the ideal height. If the actual height is too short, the render function
	 * can do whatever it wants - the buffer won't be committed. If the actual
	 * height is too tall, the render function should adapt its drawing to
	 * utilize the available space.
	 */
	double x = output->width * output->scale;
#if HAVE_TRAY
	if (bar->tray) {
		uint32_t h = render_tray(cairo, output, &x);
		max_height = h > max_height ? h : max_height;
	}
#endif
	if (bar->status) {
		uint32_t h = render_badges(cairo, output, &x);
		max_height = h > max_height ? h : max_height;
	}
	x = 0;
	if (config->workspace_buttons) {
		struct swaybar_workspace *ws;
		wl_list_for_each(ws, &output->workspaces, link) {
			uint32_t h = render_workspace_button(cairo, output, ws, &x);
			max_height = h > max_height ? h : max_height;
		}
	}
	if (config->binding_mode_indicator) {
		uint32_t h = render_binding_mode_indicator(cairo, output, x);
		max_height = h > max_height ? h : max_height;
	}

	return max_height > output->height ? max_height : output->height;
}

static void output_frame_handle_done(void *data, struct wl_callback *callback,
		uint32_t time) {
	wl_callback_destroy(callback);
	struct swaybar_output *output = data;
	output->frame_scheduled = false;
	if (output->dirty) {
		render_frame(output);
		output->dirty = false;
	}
}

static const struct wl_callback_listener output_frame_listener = {
	.done = output_frame_handle_done
};

void render_frame(struct swaybar_output *output) {
	assert(output->surface != NULL);
	if (!output->layer_surface) {
		return;
	}

	free_hotspots(&output->hotspots);

	cairo_surface_t *recorder = cairo_recording_surface_create(
			CAIRO_CONTENT_COLOR_ALPHA, NULL);
	cairo_t *cairo = cairo_create(recorder);
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
	if (output->subpixel == WL_OUTPUT_SUBPIXEL_NONE) {
		cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
	} else {
		cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
		cairo_font_options_set_subpixel_order(fo,
			to_cairo_subpixel_order(output->subpixel));
	}
	cairo_set_font_options(cairo, fo);
	cairo_font_options_destroy(fo);
	cairo_save(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cairo);
	cairo_restore(cairo);
	uint32_t height = render_to_cairo(cairo, output);
	int config_height = output->bar->config->height;
	if (config_height > 0) {
		height = config_height;
	}
	if (height != output->height || output->width == 0) {
		// Reconfigure surface
		zwlr_layer_surface_v1_set_size(output->layer_surface, 0, height);
		zwlr_layer_surface_v1_set_margin(output->layer_surface,
				output->bar->config->gaps.top,
				output->bar->config->gaps.right,
				output->bar->config->gaps.bottom,
				output->bar->config->gaps.left);
		if (strcmp(output->bar->config->mode, "dock") == 0) {
			zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, height);
		}
		// TODO: this could infinite loop if the compositor assigns us a
		// different height than what we asked for
		wl_surface_commit(output->surface);
	} else if (height > 0) {
		// Replay recording into shm and send it off
		output->current_buffer = get_next_buffer(output->bar->shm,
				output->buffers,
				output->width * output->scale,
				output->height * output->scale);
		if (!output->current_buffer) {
			cairo_surface_destroy(recorder);
			cairo_destroy(cairo);
			return;
		}
		cairo_t *shm = output->current_buffer->cairo;

		cairo_save(shm);
		cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
		cairo_paint(shm);
		cairo_restore(shm);

		cairo_set_source_surface(shm, recorder, 0.0, 0.0);
		cairo_paint(shm);

		wl_surface_set_buffer_scale(output->surface, output->scale);
		wl_surface_attach(output->surface,
				output->current_buffer->buffer, 0, 0);
		wl_surface_damage(output->surface, 0, 0,
				output->width, output->height);

		struct wl_callback *frame_callback = wl_surface_frame(output->surface);
		wl_callback_add_listener(frame_callback, &output_frame_listener, output);
		output->frame_scheduled = true;

		wl_surface_commit(output->surface);
	}
	cairo_surface_destroy(recorder);
	cairo_destroy(cairo);
}
