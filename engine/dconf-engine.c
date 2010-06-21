
#include "dconf-engine.h"
#include <gvdb-reader.h>
#include <string.h>

struct _DConfEngine
{
  gint ref_count;
};

DConfEngine *
dconf_engine_new (const gchar *context)
{
  DConfEngine *engine;

  engine = g_slice_new (DConfEngine);
  engine->ref_count = 1;

  return engine;
}

DConfEngine *
dconf_engine_ref (DConfEngine *engine)
{
  g_atomic_int_inc (&engine->ref_count);

  return engine;
}

void
dconf_engine_unref (DConfEngine *engine)
{
  g_slice_free (DConfEngine, engine);
}

GVariant *
dconf_engine_read (DConfEngine   *engine,
                   const gchar   *key,
                   DConfReadType  type)
{
  GvdbTable *table;
  GVariant *value;
  gchar *filename;

  if (type == DCONF_READ_RESET)
    return NULL;

  filename = g_build_filename (g_get_user_config_dir (), "dconf", NULL);
  table = gvdb_table_new (filename, FALSE, NULL);
  g_free (filename);

  if (table)
    {
      value = gvdb_table_get_value (table, key);
      gvdb_table_unref (table);
    }
  else
    value = NULL;

  return value;
}

static void
dconf_engine_make_match_rule (DConfEngine        *engine,
                              DConfEngineMessage *dcem,
                              const gchar        *name)
{
  gchar *rule;

  rule = g_strdup_printf ("interface='ca.desrt.dconf.Writer',"
                          "arg1path='%s'", name);
  dcem->bus_type = 'e';
  dcem->destination = "org.freedesktop.DBus";
  dcem->object_path = "/";
  dcem->interface = "org.freedesktop.DBus";
  dcem->body = g_variant_ref_sink (g_variant_new ("(s)", rule));
  g_free (rule);
}

void
dconf_engine_watch (DConfEngine        *engine,
                    DConfEngineMessage *dcem,
                    const gchar        *name)
{
  dconf_engine_make_match_rule (engine, dcem, name);
  dcem->method = "AddMatch";
}

void
dconf_engine_unwatch (DConfEngine        *engine,
                      DConfEngineMessage *dcem,
                      const gchar        *name)
{
  dconf_engine_make_match_rule (engine, dcem, name);
  dcem->method = "RemoveMatch";
}

gboolean
dconf_engine_is_writable (DConfEngine         *engine,
                          DConfEngineMessage  *dcem,
                          const gchar         *name,
                          GError             **error)
{
  dcem->bus_type = 'e';
  dcem->body = NULL;

  return TRUE;
}

static GVariant *
fake_maybe (GVariant *value)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("av"));

  if (value != NULL)
    g_variant_builder_add (&builder, "v", value);

  return g_variant_builder_end (&builder);
}

static void
dconf_engine_dcem (DConfEngine        *engine,
                   DConfEngineMessage *dcem,
                   const gchar        *method,
                   const gchar        *format_string,
                   ...)
{
  va_list ap;

  dcem->bus_type = 'e';
  dcem->destination = "ca.desrt.dconf";
  dcem->object_path = "/";
  dcem->interface = "ca.desrt.dconf.Writer";
  dcem->reply_type = G_VARIANT_TYPE ("(t)");
  dcem->method = method;

  va_start (ap, format_string);
  dcem->body = g_variant_ref_sink (g_variant_new_va (format_string,
                                                     NULL, &ap));
  va_end (ap);
}

gboolean
dconf_engine_write (DConfEngine         *engine,
                    DConfEngineMessage  *dcem,
                    const gchar         *name,
                    GVariant            *value,
                    GError             **error)
{
  dconf_engine_dcem (engine, dcem,
                     "Write", "(s@av)",
                     name, fake_maybe (value));

  return TRUE;
}

gboolean
dconf_engine_write_many (DConfEngine          *engine,
                         DConfEngineMessage   *dcem,
                         const gchar          *prefix,
                         const gchar * const  *keys,
                         GVariant            **values,
                         GError              **error)
{
  GVariantBuilder builder;
  gsize i;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sav)"));

  for (i = 0; keys[i]; i++)
    g_variant_builder_add (&builder, "(s@av)",
                           keys[i], fake_maybe (values[i]));

  dconf_engine_dcem (engine, dcem, "Merge", "(sa(sav))", prefix, &builder);

  return TRUE;
}

void
dconf_engine_set_locked (DConfEngine        *engine,
                         DConfEngineMessage *dcem,
                         const gchar        *path,
                         gboolean            locked)
{
  dconf_engine_dcem (engine, dcem, "SetLocked", "(sb)", path, locked);
}

gchar **
dconf_engine_list (DConfEngine    *engine,
                   const gchar    *dir,
                   DConfResetList *resets,
                   gsize          *length)
{
  GvdbTable *table;
  gchar *filename;
  gchar **list;

  /* not yet supported */
  g_assert (resets == NULL);

  filename = g_build_filename (g_get_user_config_dir (), "dconf", NULL);
  table = gvdb_table_new (filename, FALSE, NULL);
  g_free (filename);

  if (table)
    {
      list = gvdb_table_list (table, dir);
      gvdb_table_unref (table);
    }
  else
    list = NULL;

  if (list == NULL)
    list = g_new0 (char *, 1);

  if (length)
    *length = g_strv_length (list);

  return list;
}

static gchar *
dconf_engine_make_tag (guint        bus_type,
                       const gchar *sender,
                       guint64      seqno)
{
  return g_strdup_printf ("%c/%s/%"G_GUINT64_FORMAT, bus_type, sender, seqno);
}

gboolean
dconf_engine_decode_notify (DConfEngine   *engine,
                            const gchar   *anti_expose,
                            const gchar  **path,
                            const gchar ***rels,
                            guint          bus_type,
                            const gchar   *sender,
                            const gchar   *iface,
                            const gchar   *method,
                            GVariant      *body)
{
  guint64 seqno;
  gchar *ae;

  if (strcmp (iface, "ca.desrt.dconf.Writer") || strcmp (method, "Notify"))
    return FALSE;

  if (!g_variant_is_of_type (body, G_VARIANT_TYPE ("(tsas)")))
    return FALSE;

  g_variant_get_child (body, 0, "t", &seqno);

  if (anti_expose)
    {
      gboolean matched;

      ae = dconf_engine_make_tag (bus_type, sender, seqno);
      matched = strcmp (ae, anti_expose) == 0;
      g_free (ae);

      if (matched)
        return FALSE;
    }

  g_variant_get (body, "(t&s^a&s)", NULL, path, rels);

  return TRUE;
}

gboolean
dconf_engine_interpret_reply (DConfEngineMessage  *dcem,
                              const gchar         *sender,
                              GVariant            *body,
                              gchar              **tag,
                              GError             **error)
{
  /* typecheck and so on... */

  if (tag != NULL)
    {
      guint64 sequence;

      g_variant_get_child (body, 0, "t", &sequence);

      *tag = dconf_engine_make_tag (dcem->bus_type, sender, sequence);
    }

  return TRUE;
}
