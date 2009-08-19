/*
 * Copyright © 2009 Codethink Limited
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of version 3 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * See the included COPYING file for more information.
 *
 * Authors: Ryan Lortie <desrt@desrt.ca>
 */

#include <dbus/dbus.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>

#include "dconf-writer-config.h"
#include "dconf-writer.h"

typedef struct
{
  DConfWriter *writer;
  guint32 sequence;
  gchar *name;

  DBusConnection *bus;
  DBusMessage *this;

  GSList *signals;
} DConfDBusWriter;

static GVariant *
dconf_dbus_to_gv (DBusMessageIter *iter)
{
  switch (dbus_message_iter_get_arg_type (iter))
    {
     case DBUS_TYPE_BOOLEAN:
      {
        dbus_bool_t value;
        dbus_message_iter_get_basic (iter, &value);
        return g_variant_new_boolean (value);
      }

     case DBUS_TYPE_BYTE:
      {
        guchar value;
        dbus_message_iter_get_basic (iter, &value);
        return g_variant_new_byte (value);
      }

     case DBUS_TYPE_INT16:
      {
        gint16 value;
        dbus_message_iter_get_basic (iter, &value);
        return g_variant_new_int16 (value);
      }

     case DBUS_TYPE_UINT16:
      {
        guint16 value;
        dbus_message_iter_get_basic (iter, &value);
        return g_variant_new_uint16 (value);
      }

     case DBUS_TYPE_INT32:
      {
        gint32 value;
        dbus_message_iter_get_basic (iter, &value);
        return g_variant_new_int32 (value);
      }

     case DBUS_TYPE_UINT32:
      {
        guint32 value;
        dbus_message_iter_get_basic (iter, &value);
        return g_variant_new_uint32 (value);
      }

     case DBUS_TYPE_INT64:
      {
        gint64 value;
        dbus_message_iter_get_basic (iter, &value);
        return g_variant_new_int64 (value);
      }

     case DBUS_TYPE_UINT64:
      {
        guint64 value;
        dbus_message_iter_get_basic (iter, &value);
        return g_variant_new_uint64 (value);
      }

     case DBUS_TYPE_DOUBLE:
      {
        gdouble value;
        dbus_message_iter_get_basic (iter, &value);
        return g_variant_new_double (value);
      }

     case DBUS_TYPE_STRING:
      {
       const gchar *value;
       dbus_message_iter_get_basic (iter, &value);
       return g_variant_new_string (value);
      }

     case DBUS_TYPE_OBJECT_PATH:
      {
       const gchar *value;
       dbus_message_iter_get_basic (iter, &value);
       return g_variant_new_object_path (value);
      }

     case DBUS_TYPE_SIGNATURE:
      {
       const gchar *value;
       dbus_message_iter_get_basic (iter, &value);
       return g_variant_new_signature (value);
      }

     case DBUS_TYPE_ARRAY:
     case DBUS_TYPE_VARIANT:
     case DBUS_TYPE_STRUCT:
     case DBUS_TYPE_DICT_ENTRY:
      {
        GVariantBuilder *builder;
        GVariantTypeClass class;
        DBusMessageIter sub;
        gchar *type;

        dbus_message_iter_recurse (iter, &sub);
        class = dbus_message_iter_get_arg_type (iter);
        type = dbus_message_iter_get_signature (iter);
        builder = g_variant_builder_new (class, G_VARIANT_TYPE (type));
        g_free (type);

        while (dbus_message_iter_get_arg_type (&sub))
          {
            g_variant_builder_add_value (builder, dconf_dbus_to_gv (&sub));
            dbus_message_iter_next (&sub);
          }

        return g_variant_builder_end (builder);
      }

     default:
      g_assert_not_reached ();
    }
}

static GVariant *
dconf_dbus_variant_to_gv (DBusMessageIter *iter)
{
  DBusMessageIter sub;

  g_assert (dbus_message_iter_get_arg_type (iter) == DBUS_TYPE_VARIANT);

  dbus_message_iter_recurse (iter, &sub);
  return g_variant_ref_sink (dconf_dbus_to_gv (&sub));
}

static gboolean
dconf_dbus_writer_is_call (DConfDBusWriter *writer,
                           const gchar      *method,
                           const gchar      *signature)
{
  return dbus_message_is_method_call (writer->this,
                                      "ca.desrt.dconf.writer", method) &&
         dbus_message_has_signature (writer->this, signature);
}

static DBusMessage *
dconf_dbus_writer_reply (DConfDBusWriter *writer,
                         gboolean          success,
                         guint32          *sequence,
                         GError           *error)
{
  DBusMessage *reply;

  g_assert (success == (error == NULL));

  if (success)
    {
      reply = dbus_message_new_method_return (writer->this);

      if (sequence)
        dbus_message_append_args (reply,
                                  DBUS_TYPE_UINT32, sequence,
                                  DBUS_TYPE_INVALID);
    }
  else
    {
      reply = dbus_message_new_error (writer->this,
                                      "ca.desrt.dconf.writer.error",
                                      error->message);
      g_error_free (error);
    }

  return reply;
}

static void
dconf_dbus_writer_notify (DConfDBusWriter  *writer,
                          const gchar     *prefix,
                          const gchar    **items,
                          guint32          sequence)
{
  const gchar *my_items[] = { "", NULL };
  DBusMessageIter iter, array;
  DBusMessage *notify;
  gchar *path;

  path = g_strdup_printf ("/%s", writer->name);
  notify = dbus_message_new_signal (path, "ca.desrt.dconf.writer", "Notify");
  g_free (path);

  dbus_message_iter_init_append (notify, &iter);
  dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &prefix);
  dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY, "s", &array);

  if (items == NULL)
    items = my_items;

  while (*items)
    dbus_message_iter_append_basic (&array, DBUS_TYPE_STRING, items++);

  dbus_message_iter_close_container (&iter, &array);
  dbus_message_iter_append_basic (&iter, DBUS_TYPE_UINT32, &sequence);

  writer->signals = g_slist_prepend (writer->signals, notify);
}

static DBusMessage *
dconf_dbus_writer_handle_message (DConfDBusWriter *writer)
{
  DBusMessageIter iter;

  dbus_message_iter_init (writer->this, &iter);

  if (dconf_dbus_writer_is_call (writer, "Set", "sv"))
    {
      GError *error = NULL;
      guint32 sequence;
      const gchar *key;
      GVariant *value;
      gboolean status;

      dbus_message_iter_get_basic (&iter, &key);
      dbus_message_iter_next (&iter);
      value = dconf_dbus_variant_to_gv (&iter);
      dbus_message_iter_next (&iter);

      dconf_writer_set (writer->writer, key, value);
      status = dconf_writer_sync (writer->writer, &error);
      g_variant_unref (value);

      if (status == TRUE)
        dconf_dbus_writer_notify (writer, key, NULL,
                                  sequence = writer->sequence++);

      return dconf_dbus_writer_reply (writer, status, &sequence, error);
    }

  if (dconf_dbus_writer_is_call (writer, "Merge", "sa(sv)"))
    {
      GPtrArray *names, *values;
      DBusMessageIter array;
      GError *error = NULL;
      const gchar *prefix;
      guint32 sequence;
      gboolean status;
      gint i;

      names = g_ptr_array_new ();
      values = g_ptr_array_new ();

      dbus_message_iter_get_basic (&iter, &prefix);
      dbus_message_iter_next (&iter);
      dbus_message_iter_recurse (&iter, &array);
      while (dbus_message_iter_get_arg_type (&array))
        {
          DBusMessageIter struc;
          const gchar *name;
          GVariant *value;

          dbus_message_iter_recurse (&array, &struc);
          dbus_message_iter_get_basic (&struc, &name);
          dbus_message_iter_next (&struc);
          value = dconf_dbus_variant_to_gv (&struc);
          dbus_message_iter_next (&struc);

          dbus_message_iter_next (&array);

          g_ptr_array_add (names, (gpointer) name);
          g_ptr_array_add (values, value);
        }

      g_assert (names->len == values->len);

      dconf_writer_merge (writer->writer, prefix,
                          (const gchar **) names->pdata,
                          (GVariant **) values->pdata,
                          names->len);
      status = dconf_writer_sync (writer->writer, &error);

      for (i = 0; i < values->len; i++)
        g_variant_unref (values->pdata[i]);

      if (status == TRUE)
        {
          g_ptr_array_add (names, NULL);
          dconf_dbus_writer_notify (writer, prefix,
                                    (const gchar **) names->pdata,
                                    sequence = writer->sequence++);
        }

      g_ptr_array_free (names, TRUE);
      g_ptr_array_free (values, TRUE);

      return dconf_dbus_writer_reply (writer, status, &sequence, error);
 }

  if (dconf_dbus_writer_is_call (writer, "SetLocked", "sb"))
    {
      GError *error = NULL;
      dbus_bool_t locked;
      const gchar *key;
      gboolean status;

      dbus_message_iter_get_basic (&iter, &key);
      dbus_message_iter_next (&iter);
      dbus_message_iter_get_basic (&iter, &locked);
      dbus_message_iter_next (&iter);

      status = dconf_writer_set_locked (writer->writer,
                                         key, locked, &error);

      return dconf_dbus_writer_reply (writer, status, NULL, error);
    }

  if (dconf_dbus_writer_is_call (writer, "Unset", "s"))
    {
      GError *error = NULL;
      guint32 sequence;
      const gchar *key;
      gboolean status;

      dbus_message_iter_get_basic (&iter, &key);
      dbus_message_iter_next (&iter);

      status = dconf_writer_unset (writer->writer, key, &error);

      return dconf_dbus_writer_reply (writer, status, &sequence, error);
    }

  if (dconf_dbus_writer_is_call (writer, "Dump", ""))
    {
      dconf_writer_dump (writer->writer);
      return dconf_dbus_writer_reply (writer, TRUE, NULL, NULL);
    }

  return NULL;
}

static DBusHandlerResult
dconf_dbus_writer_filter (DBusConnection *connection,
                          DBusMessage    *message,
                          gpointer        user_data)
{
  DConfDBusWriter *writer = user_data;
  DBusMessage *reply;
  const gchar *path;

  g_assert (connection == writer->bus);
  path = dbus_message_get_path (message);

  if (path && strcmp (path + 1, writer->name) == 0)
    {
      writer->this = message;
      reply = dconf_dbus_writer_handle_message (writer);
      writer->this = NULL;

      if (reply)
        {
          dbus_connection_send (connection, reply, NULL);
          dbus_message_unref (reply);

          while (writer->signals)
            {
              dbus_connection_send (connection, writer->signals->data, NULL);
              dbus_message_unref (writer->signals->data);
              writer->signals = g_slist_delete_link (writer->signals,
                                                      writer->signals);
            }

          return DBUS_HANDLER_RESULT_HANDLED;
        }
    }

  g_assert (writer->signals == NULL);

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int
main (int argc, char **argv)
{
  DConfDBusWriter writer = { 0, };
  DConfWriterBusType bus_type;
  DBusError d_error = { 0, };
  GError *error = NULL;
  gchar *filename;
  gchar *name;

  if (argc != 2)
    {
      fprintf (stderr, "usage: %s [writername]\n", argv[0]);
      return 1;
    }

  if (!dconf_writer_config_read (argv[1], &bus_type,
                                 &writer.name,
                                 &filename, &error))
    {
      fprintf (stderr, "%s\n", error->message);
      g_error_free (error);

      return 2;
    }

  switch (bus_type)
    {
     case DCONF_WRITER_SESSION_BUS:
      writer.bus = dbus_bus_get (DBUS_BUS_SESSION, &d_error);
      break;

     case DCONF_WRITER_SYSTEM_BUS:
      writer.bus = dbus_bus_get (DBUS_BUS_SYSTEM, &d_error);
      break;

     default:
      g_assert_not_reached ();
    }

  if (writer.bus == NULL)
    {
      fprintf (stderr, "%s: %s\n", d_error.name, d_error.message);
      return 1;
    }

  name = g_strdup_printf ("ca.desrt.dconf.writer.%s", writer.name);
  if (dbus_bus_request_name (writer.bus, name, 0, &d_error) !=
      DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
      fprintf (stderr, "fatal: failed to acquire bus name %s\n", name);
      g_free (name);

      return 1;
    }
  g_free (name);

  dbus_connection_add_filter (writer.bus,
                              dconf_dbus_writer_filter,
                              &writer, NULL);

  writer.writer = dconf_writer_new (filename, &error);
  g_free (filename);

  if (writer.writer == NULL)
    {
      fprintf (stderr, "%s\n", error->message);
      g_error_free (error);

      return 2;
    }

  while (dbus_connection_read_write_dispatch (writer.bus, -1));

  return 0;
}