/*
 *   machinedrum.c
 *   Copyright (C) 2024 David García Goñi <dagargo@gmail.com>
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

#include "machinedrum.h"
#include "common.h"
#include "sds.h"

#define MACHINEDRUM_SAMPLE_LIMIT 128
#define MACHINEDRUM_SAMPLE_NAME_MAX_LEN 16
#define MACHINEDRUM_GLOBAL_SETTINGS_LEN 197

static const guint8 MACHINEDRUM_GLOBAL_SETTINGS_REQUEST[] =
  { 0xf0, 0, 0x20, 0x3c, 2, 0, 0x51, 0, 0xf7 };

enum machinedrum_fs
{
  FS_SAMPLE_MACHINEDRUM = 1
};

static gint
machinedrum_read_dir (struct backend *backend, struct item_iterator *iter,
		      const gchar *path, const gchar **extensions)
{
  struct common_simple_read_dir_data *data;

  if (strcmp (path, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct common_simple_read_dir_data));
  data->next = 0;
  data->max = MACHINEDRUM_SAMPLE_LIMIT;
  iter->data = data;
  iter->next = common_simple_next_dentry;
  iter->free = g_free;

  return 0;
}

static const struct fs_operations FS_MACHINEDRUM_SAMPLE_OPERATIONS = {
  .fs = FS_SAMPLE_MACHINEDRUM,
  .options = FS_OPTION_SAMPLE_EDITOR | FS_OPTION_MONO | FS_OPTION_SINGLE_OP |
    FS_OPTION_ID_AS_FILENAME | FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_ID,
  .name = "sample",
  .gui_name = "Samples",
  .gui_icon = BE_FILE_ICON_WAVE,
  .type_ext = "wav",
  .max_name_len = MACHINEDRUM_SAMPLE_NAME_MAX_LEN,
  .readdir = machinedrum_read_dir,
  .print_item = common_print_item,
  .rename = sds_rename,
  .download = sds_download,
  .upload = sds_upload_16b,
  .load = sds_sample_load,
  .save = sds_sample_save,
  .get_ext = backend_get_fs_ext,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = common_get_download_path
};

static GByteArray *
machinedrum_get_global_settings_dump_msg (guint id)
{
  GByteArray *tx_msg =
    g_byte_array_sized_new (sizeof (MACHINEDRUM_GLOBAL_SETTINGS_REQUEST));
  g_byte_array_append (tx_msg, MACHINEDRUM_GLOBAL_SETTINGS_REQUEST,
		       sizeof (MACHINEDRUM_GLOBAL_SETTINGS_REQUEST));
  tx_msg->data[7] = 0xf & id;
  return tx_msg;
}

static const struct fs_operations *FS_MACHINEDRUM_OPERATIONS[] = {
  &FS_MACHINEDRUM_SAMPLE_OPERATIONS, NULL
};

gint
machinedrum_handshake (struct backend *backend)
{
  gint len, err = 0;
  GByteArray *tx_msg, *rx_msg;

  tx_msg = machinedrum_get_global_settings_dump_msg (0);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg,
				    BE_SYSEX_TIMEOUT_GUESS_MS);
  if (!rx_msg)
    {
      return -ENODEV;
    }
  len = rx_msg->len;
  if (len != MACHINEDRUM_GLOBAL_SETTINGS_LEN)
    {
      err = -ENODEV;
      goto end;
    }

  backend->filesystems = FS_SAMPLE_MACHINEDRUM;
  backend->fs_ops = FS_MACHINEDRUM_OPERATIONS;
  snprintf (backend->name, LABEL_MAX, "Elektron MachineDrum");

end:
  free_msg (rx_msg);
  return err;
}
