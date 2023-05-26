/*
 *   editor.c
 *   Copyright (C) 2023 David García Goñi <dagargo@gmail.com>
 *
 *   This file is part of Elektroid.
 *
 *   Elektroid is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Elektroid is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Elektroid. If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "editor.h"
#include "sample.h"

#if defined(__linux__)
#define FRAMES_TO_PLAY (16 * 1024)
#else
#define FRAMES_TO_PLAY (64 * 1024)
#endif

#define EDITOR_PREF_CHANNELS (!editor->remote_browser->fs_ops || (editor->remote_browser->fs_ops->options & FS_OPTION_STEREO) || !editor->preferences->mix ? 2 : 1)
#define EDITOR_LOADED_CHANNELS(n) (n == 2 && sample_info->channels == 2 ? 2 : 1)

struct editor_set_volume_data
{
  struct editor *editor;
  gdouble volume;
};

extern void elektroid_set_audio_controls_on_load (gboolean sensitive);

static void
editor_set_layout_width_to_val (struct editor *editor, guint w)
{
  guint h;
  gtk_layout_get_size (GTK_LAYOUT (editor->waveform), NULL, &h);
  gtk_layout_set_size (GTK_LAYOUT (editor->waveform), w, h);
}

static void
editor_set_layout_width (struct editor *editor)
{
  guint w = gtk_widget_get_allocated_width (editor->waveform_scrolled_window);
  w = w * editor->zoom - 2;	//2 border pixels
  editor_set_layout_width_to_val (editor, w);
}

static void
editor_set_widget_source (GtkWidget * widget, enum audio_src audio_src)
{
  const char *class;
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  GList *classes, *list = gtk_style_context_list_classes (context);

  for (classes = list; classes != NULL; classes = g_list_next (classes))
    {
      gtk_style_context_remove_class (context, classes->data);
    }
  g_list_free (list);

  if (audio_src == AUDIO_SRC_NONE)
    {
      return;
    }

  if (GTK_IS_SWITCH (widget))
    {
      class = audio_src == AUDIO_SRC_LOCAL ? "local_switch" : "remote_switch";
    }
  else
    {
      class = audio_src == AUDIO_SRC_LOCAL ? "local" : "remote";
    }
  gtk_style_context_add_class (context, class);
}

void
editor_set_source (struct editor *editor, enum audio_src audio_src)
{
  if (audio_src == AUDIO_SRC_NONE)
    {
      editor_set_layout_width_to_val (editor, 1);
    }

  editor_set_widget_source (editor->autoplay_switch, audio_src);
  editor_set_widget_source (editor->mix_switch, audio_src);
  editor_set_widget_source (editor->play_button, audio_src);
  editor_set_widget_source (editor->stop_button, audio_src);
  editor_set_widget_source (editor->loop_button, audio_src);
  editor_set_widget_source (editor->volume_button, audio_src);
  editor_set_widget_source (editor->waveform, audio_src);
}

static void
editor_set_start_frame (struct editor *editor, gint start)
{
  gint max = editor->audio.frames - 1;
  start = start < 0 ? 0 : start;
  start = start > max ? max : start;

  gdouble widget_w =
    gtk_widget_get_allocated_width (editor->waveform_scrolled_window);
  GtkAdjustment *adj =
    gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW
					 (editor->waveform_scrolled_window));
  gdouble upper = widget_w * editor->zoom - 3;	//Base 0 and 2 border pixels
  gdouble lower = 0;
  gdouble value = upper * start / (double) editor->audio.frames;

  debug_print (1, "Setting waveform scrollbar to %f [%f, %f]...\n", value,
	       lower, upper);
  gtk_adjustment_set_lower (adj, 0);
  gtk_adjustment_set_upper (adj, upper);
  gtk_adjustment_set_value (adj, value);
}

static guint
editor_get_start_frame (struct editor *editor)
{
  GtkAdjustment *adj =
    gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW
					 (editor->waveform_scrolled_window));
  return editor->audio.frames * gtk_adjustment_get_value (adj) /
    (gdouble) gtk_adjustment_get_upper (adj);
}

static void
editor_set_sample_properties_on_load (struct editor *editor)
{
  double time;
  gchar label[LABEL_MAX];
  struct sample_info *sample_info = editor->audio.control.data;

  gtk_widget_set_visible (editor->sample_info_box, sample_info->frames > 0);

  if (!sample_info->frames)
    {
      return;
    }

  time = sample_info->frames / (double) sample_info->samplerate;

  gtk_widget_set_visible (editor->sample_info_box, TRUE);

  snprintf (label, LABEL_MAX, "%d", sample_info->frames);
  gtk_label_set_text (GTK_LABEL (editor->sample_length), label);

  if (time >= 60)
    {
      snprintf (label, LABEL_MAX, "%.2f %s", time / 60.0, _("minutes"));
    }
  else
    {
      snprintf (label, LABEL_MAX, "%.2f s", time);
    }
  gtk_label_set_text (GTK_LABEL (editor->sample_duration), label);

  snprintf (label, LABEL_MAX, "%.2f kHz", sample_info->samplerate / 1000.f);
  gtk_label_set_text (GTK_LABEL (editor->sample_samplerate), label);

  snprintf (label, LABEL_MAX, "%d", sample_info->channels);
  gtk_label_set_text (GTK_LABEL (editor->sample_channels), label);

  if (sample_info->bitdepth)
    {
      snprintf (label, LABEL_MAX, "%d", sample_info->bitdepth);
      gtk_label_set_text (GTK_LABEL (editor->sample_bitdepth), label);
    }

  editor_set_start_frame (editor, 0);
}

static gboolean
editor_update_ui_on_load (gpointer data)
{
  gboolean ready_to_play;
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  struct sample_info *sample_info = audio->control.data;

  g_mutex_lock (&audio->control.mutex);
  ready_to_play = audio->frames >= FRAMES_TO_PLAY || (!audio->control.active
						      && audio->frames > 0);
  audio->channels = EDITOR_LOADED_CHANNELS (editor->target_channels);
  g_mutex_unlock (&audio->control.mutex);

  editor_set_sample_properties_on_load (editor);

  if (ready_to_play)
    {
      if (audio_check (&editor->audio))
	{
	  elektroid_set_audio_controls_on_load (TRUE);
	}
      if (editor->preferences->autoplay)
	{
	  audio_play (&editor->audio);
	}
      return FALSE;
    }

  return TRUE;
}

static gboolean
editor_get_y_frame (GByteArray * sample, guint channels, guint frame,
		    guint len, gdouble * lp, gdouble * ln, gdouble * rp,
		    gdouble * rn)
{
  guint loaded_frames = sample->len >> (1 * channels);
  gshort *data = (gshort *) sample->data;
  gdouble avg_lp = 0.0, avg_ln = 0.0, avg_rp = 0.0, avg_rn = 0.0;
  guint cnt_lp = 0, cnt_ln = 0, cnt_rp = 0, cnt_rn = 0;
  gshort *s = &data[frame * channels];
  for (guint i = 0, f = frame; i < len; i++, f++)
    {
      if (f >= loaded_frames)
	{
	  return FALSE;
	}
      if (*s > 0)
	{
	  avg_lp += *s;
	  cnt_lp++;
	}
      else
	{
	  avg_ln += *s;
	  cnt_ln++;
	}
      s++;
      if (channels == 2)
	{
	  if (*s > 0)
	    {
	      avg_rp += *s;
	      cnt_rp++;
	    }
	  else
	    {
	      avg_rn += *s;
	      cnt_rn++;
	    }
	  s++;
	}
    }
  *lp = cnt_lp == 0 ? 0.0 : avg_lp / cnt_lp;
  *ln = cnt_ln == 0 ? 0.0 : avg_ln / cnt_ln;
  *rp = cnt_rp == 0 ? 0.0 : avg_rp / cnt_rp;
  *rn = cnt_rn == 0 ? 0.0 : avg_rn / cnt_rn;

  return TRUE;
}

gboolean
editor_draw_waveform (GtkWidget * widget, cairo_t * cr, gpointer data)
{
  GdkRGBA color, bgcolor;
  guint width, height, channels, x_count, layout_width;
  GtkStyleContext *context;
  gdouble x_ratio, mid_l, mid_r, value, lp, ln, rp, rn, x_frame, x_frame_next;
  gboolean stereo;
  gdouble y_scale = 1.0 / (double) SHRT_MIN;
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  struct sample_info *sample_info = audio->control.data;
  guint start = editor_get_start_frame (editor);

  debug_print (3, "Drawing waveform from %d with %dx zoom...\n",
	       start, editor->zoom);

  g_mutex_lock (&audio->control.mutex);

  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);
  channels = EDITOR_LOADED_CHANNELS (editor->target_channels);
  stereo = channels == 2;
  gtk_layout_get_size (GTK_LAYOUT (editor->waveform), &layout_width, NULL);
  x_ratio = audio->frames / (gdouble) layout_width;

  y_scale *= height;
  if (stereo)
    {
      mid_l = height * 0.25;
      mid_r = height * 0.75;
      y_scale *= 0.25;
    }
  else
    {
      mid_l = height * 0.5;
      mid_r = 0.0;
      y_scale *= 0.5;
    }

  context = gtk_widget_get_style_context (widget);
  gtk_render_background (context, cr, 0, 0, width, height);

  if (audio->frames)
    {
      GtkStateFlags state = gtk_style_context_get_state (context);
      gtk_style_context_get_color (context, state, &color);
      gtk_style_context_get_color (context, state, &bgcolor);
      bgcolor.alpha = 0.15;

      if (editor->audio.sel_len)
	{
	  gdouble x_len = editor->audio.sel_len / x_ratio;
	  gdouble x_start =
	    (editor->audio.sel_start - (gdouble) start) / x_ratio;
	  gdk_cairo_set_source_rgba (cr, &bgcolor);
	  cairo_rectangle (cr, x_start, 0, x_len, height);
	  cairo_fill (cr);
	}

      gdk_cairo_set_source_rgba (cr, &color);

      for (gint i = 0; i < width; i++)
	{
	  x_frame = start + i * x_ratio;
	  x_frame_next = x_frame + x_ratio;
	  x_count = x_frame_next - (guint) x_frame;
	  if (!x_count)
	    {
	      continue;
	    }
	  if (!editor_get_y_frame (audio->sample, channels, x_frame, x_count,
				   &lp, &ln, &rp, &rn))
	    {
	      debug_print (3,
			   "Last available frame before the sample end. Stopping...\n");
	      break;
	    }

	  value = mid_l + lp * y_scale;
	  cairo_move_to (cr, i, value);
	  value = mid_l + ln * y_scale;
	  cairo_line_to (cr, i, value);
	  cairo_stroke (cr);
	  if (stereo)
	    {
	      value = mid_r + rp * y_scale;
	      cairo_move_to (cr, i, value);
	      value = mid_r + rn * y_scale;
	      cairo_line_to (cr, i, value);
	      cairo_stroke (cr);
	    }
	}
    }

  g_mutex_unlock (&audio->control.mutex);

  return FALSE;
}

static gboolean
editor_queue_draw (gpointer data)
{
  struct editor *editor = data;
  gtk_widget_queue_draw (editor->waveform);
  return FALSE;
}

static void
editor_redraw (struct job_control *control, gpointer data)
{
  g_idle_add (editor_queue_draw, data);
}

static void
editor_load_sample_cb (struct job_control *control, gdouble p, gpointer data)
{
  set_job_control_progress_no_sync (control, p, NULL);
  editor_redraw (control, data);
}

static gpointer
editor_load_sample_runner (gpointer data)
{
  struct sample_params sample_params;
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  struct sample_info *sample_info = audio->control.data;

  editor->zoom = 1;
  editor->audio.sel_start = 0;

  sample_params.samplerate = audio->samplerate;
  sample_params.channels = editor->target_channels;

  g_timeout_add (100, editor_update_ui_on_load, editor);

  g_mutex_lock (&audio->control.mutex);
  audio->control.active = TRUE;
  g_mutex_unlock (&audio->control.mutex);

  if (sample_load_from_file_with_cb
      (audio->path, audio->sample, &audio->control, &sample_params,
       &audio->frames, editor_load_sample_cb, editor) >= 0)
    {
      debug_print (1,
		   "Samples: %d (%d B); channels: %d; loop start at %d; loop end at %d; sample rate: %.2f kHz; bit depth: %d\n",
		   audio->frames, audio->sample->len, sample_info->channels,
		   sample_info->loopstart, sample_info->loopend,
		   sample_info->samplerate / 1000.0, sample_info->bitdepth);
    }

  editor->audio.sel_len = 0;

  g_mutex_lock (&audio->control.mutex);
  audio->control.active = FALSE;
  g_mutex_unlock (&audio->control.mutex);

  editor_set_layout_width (editor);

  return NULL;
}

void
editor_play_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  audio_play (&editor->audio);
}

void
editor_stop_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  audio_stop (&editor->audio);
}

void
editor_loop_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  editor->audio.loop =
    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object));
}

gboolean
editor_autoplay_clicked (GtkWidget * object, gboolean state, gpointer data)
{
  struct editor *editor = data;
  editor->preferences->autoplay = state;
  return FALSE;
}

void
editor_start_load_thread (struct editor *editor)
{
  debug_print (1, "Creating load thread...\n");
  editor->target_channels = EDITOR_PREF_CHANNELS;
  editor->thread = g_thread_new ("load_sample", editor_load_sample_runner,
				 editor);
}

void
editor_stop_load_thread (struct editor *editor)
{
  struct audio *audio = &editor->audio;

  debug_print (1, "Stopping load thread...\n");
  g_mutex_lock (&audio->control.mutex);
  audio->control.active = FALSE;
  g_mutex_unlock (&audio->control.mutex);

  if (editor->thread)
    {
      g_thread_join (editor->thread);
      editor->thread = NULL;
    }
}

gboolean
editor_mix_clicked (GtkWidget * object, gboolean state, gpointer data)
{
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  editor->preferences->mix = state;
  if (strlen (audio->path))
    {
      audio_stop (audio);
      editor_stop_load_thread (editor);
      editor_start_load_thread (editor);
    }
  return FALSE;
}

void
editor_set_volume (GtkScaleButton * button, gdouble value, gpointer data)
{
  struct editor *editor = data;
  audio_set_volume (&editor->audio, value);
}

static gboolean
editor_set_volume_callback_bg (gpointer user_data)
{
  struct editor_set_volume_data *data = user_data;
  struct editor *editor = data->editor;
  gdouble volume = data->volume;
  g_free (data);
  debug_print (1, "Setting volume to %f...\n", volume);
  g_signal_handler_block (editor->volume_button,
			  editor->volume_changed_handler);
  gtk_scale_button_set_value (GTK_SCALE_BUTTON (editor->volume_button),
			      volume);
  g_signal_handler_unblock (editor->volume_button,
			    editor->volume_changed_handler);
  return FALSE;
}

void
editor_set_volume_callback (gpointer editor, gdouble volume)
{
  struct editor_set_volume_data *data =
    g_malloc (sizeof (struct editor_set_volume_data));
  data->editor = editor;
  data->volume = volume;
  g_idle_add (editor_set_volume_callback_bg, data);
}

static void
editor_get_frame_at_position (struct editor *editor, gdouble x,
			      guint * cursor_frame, gdouble * rel_pos)
{
  guint lw;
  guint start = editor_get_start_frame (editor);
  gtk_layout_get_size (GTK_LAYOUT (editor->waveform), &lw, NULL);
  *cursor_frame = editor->audio.frames * x / lw;
  if (rel_pos)
    {
      *rel_pos = (*cursor_frame - start) /
	(editor->audio.frames / (double) editor->zoom);
    }
}

static gboolean
editor_zoom (struct editor *editor, GdkEventScroll * event, gdouble dy)
{
  gdouble rel_pos;
  guint start, cursor_frame;
  gboolean ctrl = ((event->state) & GDK_CONTROL_MASK) != 0;

  if (!ctrl)
    {
      return FALSE;
    }

  if (dy == 0.0)
    {
      return FALSE;
    }

  g_mutex_lock (&editor->audio.control.mutex);

  editor_get_frame_at_position (editor, event->x, &cursor_frame, &rel_pos);
  debug_print (1, "Zooming at frame %d...\n", cursor_frame);

  if (dy == -1.0)
    {
      guint w;
      gtk_layout_get_size (GTK_LAYOUT (editor->waveform), &w, NULL);
      if (w >= editor->audio.frames)
	{
	  goto end;
	}
      editor->zoom = editor->zoom << 1;
    }
  else
    {
      if (editor->zoom == 1)
	{
	  goto end;
	}
      editor->zoom = editor->zoom >> 1;
    }

  debug_print (1, "Setting zoon to %dx...\n", editor->zoom);

  start = cursor_frame - rel_pos * editor->audio.frames /
    (gdouble) editor->zoom;
  editor_set_layout_width (editor);
  editor_set_start_frame (editor, start);

end:
  g_mutex_unlock (&editor->audio.control.mutex);

  return TRUE;
}

gboolean
editor_waveform_scroll (GtkWidget * widget, GdkEventScroll * event,
			gpointer data)
{
  if (event->direction == GDK_SCROLL_SMOOTH)
    {
      gdouble dx, dy;
      gdk_event_get_scroll_deltas ((GdkEvent *) event, &dx, &dy);
      if (editor_zoom (data, event, dy))
	{
	  g_idle_add (editor_queue_draw, data);
	}
    }
  return FALSE;
}

static void
editor_on_size_allocate (GtkWidget * self, GtkAllocation * allocation,
			 struct editor *editor)
{
  if (editor->audio.frames == 0)
    {
      return;
    }

  guint start = editor_get_start_frame (editor);
  editor_set_layout_width (editor);
  editor_set_start_frame (editor, start);
}

static gboolean
editor_button_press (GtkWidget * widget, GdkEventButton * event,
		     gpointer data)
{
  guint cursor_frame;
  struct editor *editor = data;

  audio_stop (&editor->audio);
  editor->selecting = TRUE;
  editor->audio.sel_len = 0;
  gtk_widget_grab_focus (editor->waveform_scrolled_window);

  editor_get_frame_at_position (data, event->x, &cursor_frame, NULL);
  debug_print (2, "Pressing at frame %d...\n", cursor_frame);
  editor->audio.sel_start = cursor_frame;

  g_idle_add (editor_queue_draw, data);
  return FALSE;
}

static gboolean
editor_button_release (GtkWidget * widget, GdkEventButton * event,
		       gpointer data)
{
  struct editor *editor = data;

  editor->selecting = FALSE;
  gtk_widget_grab_focus (editor->waveform_scrolled_window);

  if (editor->audio.sel_len < 0)
    {
      editor->audio.sel_start += editor->audio.sel_len;
      editor->audio.sel_len = -editor->audio.sel_len;
    }

  g_idle_add (editor_queue_draw, data);

  if (editor->preferences->autoplay && editor->audio.sel_len)
    {
      audio_play (&editor->audio);
    }

  return FALSE;
}

static gboolean
editor_motion_notify (GtkWidget * widget, GdkEventMotion * event,
		      gpointer data)
{
  guint cursor_frame;
  struct editor *editor = data;
  if (editor->selecting)
    {
      editor_get_frame_at_position (editor, event->x, &cursor_frame, NULL);
      debug_print (3, "Motion over sample %d...\n", cursor_frame);
      editor->audio.sel_len =
	((glong) cursor_frame) - editor->audio.sel_start;

      g_idle_add (editor_queue_draw, data);
    }
  return FALSE;
}

void
editor_init (struct editor *editor)
{
  g_signal_connect (editor->waveform_scrolled_window, "size-allocate",
		    G_CALLBACK (editor_on_size_allocate), editor);
  gtk_widget_add_events (editor->waveform, GDK_BUTTON_PRESS_MASK);
  g_signal_connect (editor->waveform, "button-press-event",
		    G_CALLBACK (editor_button_press), editor);
  gtk_widget_add_events (editor->waveform, GDK_BUTTON_RELEASE_MASK);
  g_signal_connect (editor->waveform, "button-release-event",
		    G_CALLBACK (editor_button_release), editor);
  gtk_widget_add_events (editor->waveform, GDK_POINTER_MOTION_MASK);
  g_signal_connect (editor->waveform, "motion-notify-event",
		    G_CALLBACK (editor_motion_notify), editor);
}
