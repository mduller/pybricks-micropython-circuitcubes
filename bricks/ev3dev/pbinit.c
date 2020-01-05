
// SPDX-License-Identifier: MIT
// Copyright (c) 2018 Laurens Valk

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/timerfd.h>

#include <glib.h>
#include <grx-3.0.h>

#include <pbio/main.h>
#include <pbio/light.h>

#include "py/mpthread.h"

#include "pbinit.h"

#define PERIOD_MS 10

struct periodic_info {
    int timer_fd;
    unsigned long long wakeups_missed;
};

// Configure timer at specified interval
static int configure_timer_thread(unsigned int period_ms, struct periodic_info *info)
{
    struct itimerspec itval;
    int fd = timerfd_create(CLOCK_MONOTONIC, 0);
    info->wakeups_missed = 0;
    info->timer_fd = fd;
    if (fd == -1){
        return fd;
    }
    itval.it_interval.tv_sec = 0;
    itval.it_interval.tv_nsec = period_ms * 1000000;
    itval.it_value.tv_sec = 0;
    itval.it_value.tv_nsec = period_ms * 1000000;
    return timerfd_settime(fd, 0, &itval, NULL);
}

// Wait for timer to complete period
static void wait_period(struct periodic_info *info)
{
    unsigned long long missed;
    int ret;
    // Wait for the next timer event. If we have missed any the number is written to "missed".
    ret = read(info->timer_fd, &missed, sizeof(missed));
    if (ret == -1) {
        perror("Unable to read timer");
        return;
    }
    info->wakeups_missed += missed;
}

// Flag that indicates whether we are busy stopping the thread
volatile bool stopping_thread = false;

// The background thread that keeps firing the task handler
static void *task_caller(void *arg)
{
    struct periodic_info info;
    configure_timer_thread(PERIOD_MS, &info);
    while (!stopping_thread) {
        MP_THREAD_GIL_ENTER();
        while (pbio_do_one_event()) { }
        MP_THREAD_GIL_EXIT();
        wait_period(&info); // TODO: check if we should do any waiting here
    }
    // Signal that shutdown is complete
    stopping_thread = false;
    return NULL;
}

static guint startup_animation_source;

static gboolean update_startup_animation(gpointer user_data) {
    static GrxContext *context = NULL;
    static gint angle = 0;

    if (!context) {
        context = grx_context_new(grx_get_screen_width(), grx_get_screen_height(), NULL, NULL);
        if (!context) {
            g_warning("Failed to create graphics context for animation");
            startup_animation_source = 0;
            return G_SOURCE_REMOVE;
        }
    }

    grx_set_current_context(context);

    gint x = grx_get_width() / 2;
    gint y = grx_get_height() / 2;
    gint r = 9 * MIN(x, y) / 10;
    gint end = angle - 45;

    grx_clear_context(GRX_COLOR_WHITE);
    grx_draw_filled_circle_arc(x, y, r, angle * 10, end * 10,
        GRX_ARC_STYLE_CLOSED_RADIUS, GRX_COLOR_BLACK);
    grx_draw_filled_circle(x, y, 2 * r / 3, GRX_COLOR_WHITE);
    grx_context_bit_blt(grx_get_screen_context(), 0, 0, NULL, 0, 0,
        grx_get_max_x(), grx_get_max_y(), GRX_COLOR_MODE_WRITE);

    angle += 5;

    return G_SOURCE_CONTINUE;
}

void pbricks_end_startup_animation() {
    if (startup_animation_source) {
        g_source_remove(startup_animation_source);
        startup_animation_source = 0;
    }
}

// Pybricks initialization tasks
void pybricks_init() {
    GError *error = NULL;
    if (!grx_set_mode_default_graphics(FALSE, &error)) {
        fprintf(stderr, "Could not initialize graphics. Be sure to run using `brickrun -r -- pybricks-micropython`.\n");
        exit(1);
    }
    grx_clear_screen(GRX_COLOR_WHITE);
    update_startup_animation(NULL);
    startup_animation_source = g_timeout_add(200, update_startup_animation, NULL);

    pbio_init();
    pbio_light_on_with_pattern(PBIO_PORT_SELF, PBIO_LIGHT_COLOR_GREEN, PBIO_LIGHT_PATTERN_BREATHE); // TODO: define PBIO_LIGHT_PATTERN_EV3_RUN (Or, discuss if we want to use breathe for EV3, too)
    pthread_t task_caller_thread;
    pthread_create(&task_caller_thread, NULL, task_caller, NULL);
}

// Pybricks deinitialization tasks
void pybricks_deinit(){
    // Signal motor thread to stop and wait for it to do so.
    stopping_thread = true;
    while (stopping_thread);
    pbio_deinit();
}
