#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include "swaybar/badges.h"
#include "swaybar/badges_internal.h"
#include "log.h"

#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>
#include <pulse/stream.h>

#define group ((struct group_audio_t*)user)

enum {
	DEVICE_SINK = 0,
	DEVICE_SOURCE,
	DEVICE_MAX
};

struct group_audio_t {
	struct badges_t *B;

	struct badge_t *badge_input;
	struct badge_t *badge_output;

	pa_threaded_mainloop *pa_loop;
	pa_context *ctx;

#define SINK_INDICES_SIZ (4)
	uint32_t sink_indices[SINK_INDICES_SIZ];
	int sink_counter;

	double volume;

#define OUTPUT_BUFFER_SIZ (128)
	char output_buffer[OUTPUT_BUFFER_SIZ];
};

static double volume_percent(pa_volume_t vol) {
	return vol / (double)PA_VOLUME_NORM;
}

static void update_volume(const struct pa_sink_info *i, void *user) {

	uint32_t avg = pa_cvolume_avg(&i->volume);
	sway_log(SWAY_DEBUG, "sink: '%s' vol_avg=%u", i->name, avg);

	if(avg != PA_VOLUME_MUTED) {
		double volume = volume_percent(avg);
		group->volume = volume;
		int percent_int = (volume * 100);
		snprintf(group->output_buffer, OUTPUT_BUFFER_SIZ-1, "VOL %d%%", percent_int);
		map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, group->badge_output);
	} else {
		snprintf(group->output_buffer, OUTPUT_BUFFER_SIZ-1, "VOL MUTED");
		map_badge_quality_to_colors(BADGE_QUALITY_ERROR, group->badge_output);
	}
}

// Do we know about the sink with a given index
static int sink_is_known(uint32_t index, void* user) {
	for(int i = 0; i < SINK_INDICES_SIZ; i++) {
		if(group->sink_indices[i] == index) {
			return 1;
		}
	}
	return 0;
}

static void sink_added(uint32_t index, void *user) {
	assert(group->sink_counter < SINK_INDICES_SIZ);

	for(int i = 0; i < SINK_INDICES_SIZ; i++) {
		if(group->sink_indices[i] == 0xFFFFFFFF) {
			group->sink_indices[i] = index;
			group->sink_counter++;
			break;
		}
	}

	if(group->badge_output == NULL) {
		sway_log(SWAY_DEBUG, "Creating output badge");
		group->badge_output = create_badge(group->B);
		group->badge_output->text = group->output_buffer;
		map_badge_quality_to_colors(BADGE_QUALITY_NORMAL, group->badge_output);
		group->badge_output->anim.should_be_visible = 1;
	}
}

static void sink_removed(uint32_t index, void *user) {
	for(int i = 0; i < SINK_INDICES_SIZ; i++) {
		if(group->sink_indices[i] == index) {
			group->sink_counter--;
			group->sink_indices[i] = 0xFFFFFFFF;
			break;
		}
	}

	if(group->sink_counter == 0 && group->badge_output != NULL) {
		destroy_badge(group->B, group->badge_output);
		group->badge_output = NULL;
	}
}

static void sink_cb(pa_context *c, const pa_sink_info *i, int eol, void *user) {
	if(eol < 0) {
		if(pa_context_errno(c) == PA_ERR_NOENTITY) {
			return;
		}

		sway_log(SWAY_ERROR, "Sink callback failure");
		return;
	}

	if(eol > 0) {
		return;
	}

	if(!sink_is_known(i->index, user)) {
		sway_log(SWAY_DEBUG, "New sink detected: '%s'", i->name);
		sink_added(i->index, user);
	}

	update_volume(i, user);
}

#define SUBSCRIPTION_MASK (pa_subscription_mask_t)(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE)

static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *user) {
	pa_operation *o;
	switch(t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) {
		case PA_SUBSCRIPTION_EVENT_SINK:
			if((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
				sink_removed(index, user);
				return;
			}
			if(!(o = pa_context_get_sink_info_by_index(c, index, sink_cb, user))) {
				sway_log(SWAY_ERROR, "pa_context-get_sink_info_by_index() failed");
				return;
			}
			pa_operation_unref(o);
			break;
		default:
			break;
	}
}

static void context_state_cb(pa_context *c, void *user) {
	switch(pa_context_get_state(c)) {
		case PA_CONTEXT_READY:
		{
			pa_operation *o;

			pa_context_set_subscribe_callback(group->ctx, subscribe_cb, group);
			if(!(o = pa_context_subscribe(group->ctx, SUBSCRIPTION_MASK, NULL, NULL))) {
				sway_log(SWAY_ERROR, "pa_context_subscribe() failed");
				return;
			}
			pa_operation_unref(o);

			if(!(o = pa_context_get_sink_info_list(group->ctx, sink_cb, group))) {
				sway_log(SWAY_ERROR, "pa_context_get_sink_info_list() failed");
				return;
			}
			pa_operation_unref(o);

			break;
		}
		default:
		{
			break;
		}
	}
}

static void setup_pa(struct group_audio_t *user) {
	group->ctx = NULL;

	group->pa_loop = pa_threaded_mainloop_new();
	if(group->pa_loop == NULL) {
		sway_log(SWAY_ERROR, "Failed to create threaded mainloop");
		return;
	}

	pa_threaded_mainloop_start(group->pa_loop);

	pa_mainloop_api *ml_api = pa_threaded_mainloop_get_api(group->pa_loop);
	group->ctx = pa_context_new(ml_api, "net.easimer.swaybar");
	if(group->ctx == NULL) {
		sway_log(SWAY_ERROR, "Failed to create PA context");
		goto free_mainloop;
	}

	pa_context_set_state_callback(group->ctx, context_state_cb, group);

	if(pa_context_connect(group->ctx, NULL, PA_CONTEXT_NOFAIL, NULL) < 0) {
		sway_log(SWAY_ERROR, "Context connection failed");
		goto free_context;
	}

	return;
free_context:
	pa_context_unref(group->ctx);
	group->ctx = NULL;
free_mainloop:
	pa_threaded_mainloop_stop(group->pa_loop);
	pa_threaded_mainloop_free(group->pa_loop);
	group->pa_loop = NULL;
}

static void* setup(struct badges_t *B) {
	struct group_audio_t *g = malloc(sizeof(struct group_audio_t));

	g->B = B;

	g->badge_input = NULL;

	g->badge_output = NULL;
	g->output_buffer[0] = '\0';
	for(int i = 0; i < SINK_INDICES_SIZ; i++) {
		g->sink_indices[i] = 0xFFFFFFFF;
	}
	g->sink_counter = 0;

	setup_pa(g);

	return g;
}

static void update(struct badges_t *B, void *user, double dt) {
}

static void cleanup(struct badges_t *B, void *user) {
	if(group->pa_loop != NULL) {
		pa_threaded_mainloop_stop(group->pa_loop);
		pa_threaded_mainloop_free(group->pa_loop);
	}

	if(group->ctx != NULL) {
		pa_context_unref(group->ctx);
	}

	destroy_badge(B, group->badge_input);
	destroy_badge(B, group->badge_output);
	free(group);
}

static struct badge_group_t _group = {
	.setup = setup,
	.update = update,
	.cleanup = cleanup,
};

DEFINE_BADGE_GROUP_REGISTER(audio) {
	register_badge_group(B, &_group);
}
