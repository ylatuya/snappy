/*
 * Dogme media player.
 *
 * Copyright (C) 2011 Collabora Multimedia Ltd.
 * <luis.debethencourt@collabora.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <string.h>
#include <clutter/clutter.h>
#include <clutter-gst/clutter-gst.h>

#include "user_interface.h"
#include "utils.h"

// Declaration of static functions
static void center_controls (UserInterface *ui);
static gboolean controls_timeout_cb (gpointer data);
static gboolean event_cb (ClutterStage *stage, ClutterEvent *event,
							gpointer data);
static gboolean progress_update (gpointer data);
static void size_change (ClutterStage *stage, gpointer *data);
static void show_controls (UserInterface *ui, gboolean vis);
static void toggle_fullscreen (UserInterface *ui);
static void toggle_playing (UserInterface *ui, GstEngine *engine);


/* ---------------------- static functions ----------------------- */

static void
center_controls (UserInterface *ui)
{
	gfloat x, y;

	x = (ui->stage_width - clutter_actor_get_width (ui->controls)) / 2;
	y = ui->stage_height - (ui->stage_height / 3);

	g_debug ("stage width = %.2d, height = %.2d\n", ui->stage_width,
		ui->stage_height);
	g_debug ("setting x = %.2f, y = %.2f, width = %.2f\n",
		x, y, clutter_actor_get_width (ui->controls));

	clutter_actor_set_position (ui->controls, x, y);
}

static gboolean
controls_timeout_cb (gpointer data)
{
	UserInterface *ui = data;

	ui->controls_timeout = 0;
	show_controls (ui, FALSE);

	return FALSE;
}

static gboolean
event_cb (ClutterStage *stage,
			ClutterEvent *event,
			gpointer data)
{
	UserInterface *ui = (UserInterface*)data;
	gboolean handled = FALSE;

	switch (event->type) {
		case CLUTTER_KEY_PRESS:
		{
			ClutterVertex center = { 0, };
			ClutterAnimation *animation = NULL;

			center.x - clutter_actor_get_width (ui->texture) / 2;
			guint keyval = clutter_event_get_key_symbol (event);
			switch (keyval) {
				case CLUTTER_q:
				case CLUTTER_Escape:
					clutter_main_quit ();
					break;
				case CLUTTER_f:
					// Fullscreen button
					toggle_fullscreen (ui);
					handled = TRUE;
					break;
				case CLUTTER_space:
					// Spacebar
					toggle_playing (ui, ui->engine);
					handled = TRUE;
					break;
				case CLUTTER_8:
				{
					// Mute button
					gboolean muteval;
					g_object_get (G_OBJECT (ui->engine->player), "mute",
									&muteval, NULL);
					g_object_set (G_OBJECT (ui->engine->player), "mute",
									! muteval, NULL);
					if (muteval) {
						g_debug ("Unmute stream\n");
					} else {
						g_debug ("Mute stream\n");
					}
					handled = TRUE;
					break;
				}

				case CLUTTER_9:
				case CLUTTER_0:
				{
					gdouble volume;
					g_object_get (G_OBJECT (ui->engine->player), "volume",
									&volume, NULL);
					// Volume Down
					if (keyval == CLUTTER_9 && volume > 0.0) {
						g_object_set (G_OBJECT (ui->engine->player), "volume",
										volume -= 0.05, NULL);
						g_debug ("Volume down: %f", volume);

					// Volume Up
					} else if (keyval == CLUTTER_0 && volume < 1.0) {
						g_object_set (G_OBJECT (ui->engine->player), "volume",
										volume += 0.05, NULL);
						g_debug ("Volume up: %f", volume);
					}
					handled = TRUE;
					break;
				}

				case CLUTTER_Up:
				case CLUTTER_Down:
				case CLUTTER_Left:
				case CLUTTER_Right:
				{
					gint64 pos;
					GstFormat fmt = GST_FORMAT_TIME;
					gst_element_query_position (ui->engine->player, &fmt,
												&pos);
					// Seek 1 minute foward
					if (keyval == CLUTTER_Up) {
						pos += 60 * GST_SECOND;
						g_debug("Skipping 1 minute ahead in the stream\n");

					// Seek 1 minute back
					} else if (keyval == CLUTTER_Down) {
						pos -= 60 * GST_SECOND;
						g_debug("Moving 1 minute back in the stream\n");

					// Seek 10 seconds back
					} else if (keyval == CLUTTER_Left) {
						pos -= 10 * GST_SECOND;
						g_debug("Moving 10 seconds back in the stream\n");

					// Seek 10 seconds foward
					} else if (keyval == CLUTTER_Right) {
						pos += 10 * GST_SECOND;
						g_debug("Skipping 10 seconds ahead in the stream\n");
					}

					gst_element_seek_simple (ui->engine->player,
											fmt, GST_SEEK_FLAG_FLUSH,
											pos);

					gfloat progress = (float) pos / ui->engine->media_duration;
					clutter_actor_set_size (ui->control_seekbar,
											progress * SEEK_WIDTH,
											SEEK_HEIGHT);

					handled = TRUE;
					break;
				}
				case CLUTTER_r:
					// rotate texture 90 degrees.
					handled = TRUE;
					break;

				default:
					handled = FALSE;
					break;
			}
		}

		case CLUTTER_BUTTON_PRESS:
		{
			if (ui->controls_showing) {
				ClutterActor *actor;
				ClutterButtonEvent *bev = (ClutterButtonEvent *) event;

				actor = clutter_stage_get_actor_at_pos (stage,
							CLUTTER_PICK_ALL, bev->x, bev->y);
				if (actor == ui->control_play_toggle) {
					toggle_playing (ui, ui->engine);
				}
				else if (actor == ui->control_seek1 ||
						actor == ui->control_seek2 ||
						actor == ui->control_seekbar) {
					gfloat x, y, dist;
					gint64 progress;

					clutter_actor_get_transformed_position (ui->control_seekbar,
															&x, &y);
					dist = bev->x - x;
					dist = CLAMP (dist, 0, SEEK_WIDTH);

					if (ui->engine->media_duration == -1)
					{
						update_media_duration (ui->engine);
					}

					progress = ui->engine->media_duration * (dist / SEEK_WIDTH);
					gst_element_seek_simple (ui->engine->player,
										GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
										progress);
					clutter_actor_set_size (ui->control_seekbar,
							dist, SEEK_HEIGHT);
				}
			}
			handled = TRUE;
			break;
		}

		case CLUTTER_MOTION:
		{
			show_controls (ui, TRUE);
			handled = TRUE;
			break;
		}
	}

	return handled;
}

static gboolean
progress_update (gpointer data)
{
	UserInterface *ui = (UserInterface*)data;
	GstEngine *engine = ui->engine;
	gfloat progress = 0.0;

	if (engine->media_duration == -1)
	{
		update_media_duration (engine);
	}

	gint64 pos;
	GstFormat fmt = GST_FORMAT_TIME;
	gst_element_query_position (engine->player, &fmt, &pos);
	progress = (float) pos / engine->media_duration;
	g_debug ("playback position progress: %f\n", progress);

	clutter_actor_set_size (ui->control_seekbar, progress * SEEK_WIDTH,
	SEEK_HEIGHT);

	return TRUE;
}

static void
size_change (ClutterStage *stage,
				gpointer *data)
{
	UserInterface *ui = (UserInterface*)data;

	gfloat stage_width, stage_height;
	gfloat new_width, new_height;
	gfloat media_width, media_height;
	gfloat center, aratio;

	media_width = ui->engine->media_width;
	media_height = ui->engine->media_height;

	stage_width = clutter_actor_get_width (ui->stage);
	stage_height = clutter_actor_get_height (ui->stage);
	ui->stage_width = stage_width;
	ui->stage_height = stage_height;

	new_width = stage_width;
	new_height = stage_height;
	if (media_height <= media_width)
	{
		aratio = media_height / media_width;
		new_height = new_width * aratio;
		center = (stage_height - new_height) / 2;
		clutter_actor_set_position (CLUTTER_ACTOR (ui->texture), 0, center);
	} else {
		aratio = media_width / media_height;
		new_width = new_height * aratio;
		center = (stage_width - new_width) / 2;
		clutter_actor_set_position (CLUTTER_ACTOR (ui->texture), center, 0);
	}

	clutter_actor_set_size (CLUTTER_ACTOR (ui->texture),
							new_width, new_height);
	center_controls(ui);
}

static void
show_controls (UserInterface *ui, gboolean vis)
{
	if (vis == TRUE && ui->controls_showing == TRUE)
	{
		if (ui->controls_timeout == 0)
		{
			ui->controls_timeout =
				g_timeout_add_seconds (3, controls_timeout_cb, ui);
		}

		return;
	}

	if (vis == TRUE && ui->controls_showing == FALSE)
	{
		ui->controls_showing = TRUE;

		clutter_stage_show_cursor (CLUTTER_STAGE (ui->stage));
		clutter_actor_animate (ui->controls, CLUTTER_EASE_OUT_QUINT, 250,
			"opacity", 224,
			NULL);

		return;
	}

	if (vis == FALSE && ui->controls_showing == TRUE)
	{
		ui->controls_showing = FALSE;

		clutter_stage_hide_cursor (CLUTTER_STAGE (ui->stage));
		clutter_actor_animate (ui->controls, CLUTTER_EASE_OUT_QUINT, 250,
			"opacity", 0,
			NULL);
		return;
	}
}

static void
toggle_fullscreen (UserInterface *ui)
{
	if (ui->fullscreen) {
		clutter_stage_set_fullscreen (CLUTTER_STAGE (ui->stage), FALSE);
		ui->fullscreen = FALSE;
	} else {
		clutter_stage_set_fullscreen (CLUTTER_STAGE (ui->stage), TRUE);
		ui->fullscreen = TRUE;
	}
}

static void
toggle_playing (UserInterface *ui, GstEngine *engine)
{
	if (engine->playing) {
		gst_element_set_state (engine->player, GST_STATE_PAUSED);
		engine->playing = FALSE;

		clutter_texture_set_from_file (
					CLUTTER_TEXTURE (ui->control_play_toggle), ui->play_png,
					NULL);
	} else {
		gst_element_set_state (engine->player, GST_STATE_PLAYING);
		engine->playing = TRUE;

		clutter_texture_set_from_file (
					CLUTTER_TEXTURE (ui->control_play_toggle), ui->pause_png,
					NULL);
	}
}

/* -------------------- non-static functions --------------------- */

void
load_user_interface (UserInterface *ui)
{
	// Stage
	ClutterColor stage_color = { 0x00, 0x00, 0x00, 0x00 };
	ClutterColor control_color1 = { 73, 74, 77, 0xee };
	ClutterColor control_color2 = { 0xcc, 0xcc, 0xcc, 0xff };
	ui->filename = g_path_get_basename (ui->filepath);

	ui->media_width = ui->engine->media_width;
	ui->media_height = ui->engine->media_height;
	ui->stage_width = ui->engine->media_width;
	ui->stage_height = ui->engine->media_height;
	ui->stage = clutter_stage_get_default();
	clutter_stage_set_color (CLUTTER_STAGE (ui->stage), &stage_color);
	clutter_stage_set_minimum_size (CLUTTER_STAGE (ui->stage),
									ui->stage_width, ui->stage_height);
	clutter_stage_set_title (CLUTTER_STAGE (ui->stage), ui->filename);

	if (ui->fullscreen) {
		clutter_stage_set_fullscreen (CLUTTER_STAGE (ui->stage), TRUE);
	} else {
		clutter_actor_set_size (CLUTTER_ACTOR (ui->stage), ui->stage_width,
								ui->stage_height);
	}

	// Check icon files exist
	gchar *vid_panel_png = g_strdup_printf ("%s%s", DOGME_DATA_DIR,
											"/vid-panel.png");
	ui->play_png = g_strdup_printf ("%s%s", DOGME_DATA_DIR,
										"/media-actions-start.png");
	ui->pause_png = g_strdup_printf ("%s%s", DOGME_DATA_DIR,
										"/media-actions-pause.png");
	gchar *icon_files[3];
	icon_files[0] = vid_panel_png;
	icon_files[1] = ui->play_png;
	icon_files[2] = ui->pause_png;

	gint c;
	for (c = 0; c < 3; c++) {
		if (!g_file_test (icon_files[c], G_FILE_TEST_EXISTS))
		{
			g_print ("Icon file doesn't exist, are you sure you have " \
					" installed dogme correctly?\nThe file needed is: %s\n",
					icon_files[c]);

		}
	}

	// Controls
	ClutterLayoutManager *controls_layout = clutter_bin_layout_new (
												CLUTTER_BIN_ALIGNMENT_FILL,
												CLUTTER_BIN_ALIGNMENT_FILL);
	ui->controls = clutter_box_new (controls_layout);

	ui->control_bg =
			clutter_texture_new_from_file (vid_panel_png, NULL);
	clutter_container_add_actor (CLUTTER_CONTAINER (ui->controls),
									ui->control_bg);
	g_free (vid_panel_png);

	ui->control_play_toggle =
			clutter_texture_new_from_file (ui->pause_png, NULL);
	clutter_bin_layout_add (CLUTTER_BIN_LAYOUT (controls_layout),
							ui->control_play_toggle,
							CLUTTER_BIN_ALIGNMENT_FIXED,
							CLUTTER_BIN_ALIGNMENT_FIXED);

	ui->control_title =
			clutter_text_new_full ("Sans Bold 24", cut_long_filename (ui->filename),
									&control_color1);
	clutter_bin_layout_add (CLUTTER_BIN_LAYOUT (controls_layout),
							ui->control_title,
							CLUTTER_BIN_ALIGNMENT_FIXED,
							CLUTTER_BIN_ALIGNMENT_FIXED);

	ui->control_seek1   = clutter_rectangle_new_with_color (&control_color1);
	clutter_bin_layout_add (CLUTTER_BIN_LAYOUT (controls_layout),
							ui->control_seek1,
							CLUTTER_BIN_ALIGNMENT_FIXED,
							CLUTTER_BIN_ALIGNMENT_FIXED);

	ui->control_seek2   = clutter_rectangle_new_with_color (&control_color2);
	clutter_bin_layout_add (CLUTTER_BIN_LAYOUT (controls_layout),
							ui->control_seek2,
							CLUTTER_BIN_ALIGNMENT_FIXED,
							CLUTTER_BIN_ALIGNMENT_FIXED);

	ui->control_seekbar = clutter_rectangle_new_with_color (&control_color1);
	clutter_bin_layout_add (CLUTTER_BIN_LAYOUT (controls_layout),
							ui->control_seekbar,
							CLUTTER_BIN_ALIGNMENT_FIXED,
							CLUTTER_BIN_ALIGNMENT_FIXED);

	clutter_actor_set_opacity (ui->controls, 0xee);

	clutter_actor_set_position (ui->control_play_toggle, 30, 30);

	clutter_actor_set_size (ui->control_seek1, SEEK_WIDTH+10, SEEK_HEIGHT+10);
	clutter_actor_set_position (ui->control_seek1, 200, 100);
	clutter_actor_set_size (ui->control_seek2, SEEK_WIDTH, SEEK_HEIGHT);
	clutter_actor_set_position (ui->control_seek2, 205, 105);
	clutter_actor_set_size (ui->control_seekbar, 0, SEEK_HEIGHT);
	clutter_actor_set_position (ui->control_seekbar, 205, 105);

	clutter_actor_set_position (ui->control_title, 200, 40);

	g_assert (ui->control_bg && ui->control_play_toggle);

	// Add control UI to stage
	clutter_container_add (CLUTTER_CONTAINER (ui->stage),
							ui->texture,
							ui->controls,
							NULL);

	clutter_stage_hide_cursor (CLUTTER_STAGE (ui->stage));
	clutter_actor_animate (ui->controls, CLUTTER_EASE_OUT_QUINT, 1000,
							"opacity", 0, NULL);

	g_signal_connect (CLUTTER_TEXTURE (ui->stage), "fullscreen",
						G_CALLBACK (size_change), ui);
	g_signal_connect (CLUTTER_TEXTURE (ui->stage), "unfullscreen",
						G_CALLBACK (size_change), ui);
	g_signal_connect (ui->stage, "event", G_CALLBACK (event_cb), ui);

	g_timeout_add (2000, progress_update, ui);

	center_controls (ui);
	clutter_actor_show(ui->stage);
}