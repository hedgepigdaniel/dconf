#include "../common/dconf-changeset.h"

static gboolean
should_not_run (const gchar *key,
                GVariant    *value,
                gpointer     user_data)
{
  g_assert_not_reached ();
}

static gboolean
is_null (const gchar *key,
         GVariant    *value,
         gpointer     user_data)
{
  return value == NULL;
}

static gboolean
is_not_null (const gchar *key,
             GVariant    *value,
             gpointer     user_data)
{
  return value != NULL;
}

static void
test_basic (void)
{
  DConfChangeset *changeset;
  gboolean result;
  GVariant *value;
  gint n_items;

  changeset = dconf_changeset_new ();
  dconf_changeset_ref (changeset);
  dconf_changeset_all (changeset, should_not_run, NULL);
  n_items = dconf_changeset_describe (changeset, NULL, NULL, NULL);
  g_assert_cmpint (n_items, ==, 0);
  dconf_changeset_unref (changeset);
  dconf_changeset_unref (changeset);

  changeset = dconf_changeset_new_write ("/value/a", NULL);
  result = dconf_changeset_all (changeset, is_null, NULL);
  g_assert (result);
  result = dconf_changeset_all (changeset, is_not_null, NULL);
  g_assert (!result);

  result = dconf_changeset_get (changeset, "/value/a", &value);
  g_assert (result);
  g_assert (value == NULL);

  result = dconf_changeset_get (changeset, "/value/b", &value);
  g_assert (!result);

  dconf_changeset_set (changeset, "/value/b", g_variant_new_int32 (123));
  result = dconf_changeset_all (changeset, is_null, NULL);
  g_assert (!result);
  result = dconf_changeset_all (changeset, is_not_null, NULL);
  g_assert (!result);

  result = dconf_changeset_get (changeset, "/value/a", &value);
  g_assert (result);
  g_assert (value == NULL);

  result = dconf_changeset_get (changeset, "/value/b", &value);
  g_assert (result);
  g_assert_cmpint (g_variant_get_int32 (value), ==, 123);
  g_variant_unref (value);

  dconf_changeset_set (changeset, "/value/a", g_variant_new_string ("a string"));
  result = dconf_changeset_all (changeset, is_null, NULL);
  g_assert (!result);
  result = dconf_changeset_all (changeset, is_not_null, NULL);
  g_assert (result);

  result = dconf_changeset_get (changeset, "/value/a", &value);
  g_assert (result);
  g_assert_cmpstr (g_variant_get_string (value, NULL), ==, "a string");
  g_variant_unref (value);

  result = dconf_changeset_get (changeset, "/value/b", &value);
  g_assert (result);
  g_assert_cmpint (g_variant_get_int32 (value), ==, 123);
  g_variant_unref (value);

  dconf_changeset_unref (changeset);
}

static void
test_similarity (void)
{
  DConfChangeset *a, *b;

  a = dconf_changeset_new ();
  b = dconf_changeset_new ();

  g_assert (dconf_changeset_is_similar_to (a, b));
  g_assert (dconf_changeset_is_similar_to (b, a));

  dconf_changeset_set (a, "/value/a", g_variant_new_int32 (0));
  g_assert (!dconf_changeset_is_similar_to (a, b));
  g_assert (!dconf_changeset_is_similar_to (b, a));

  /* different values for the same key are still the same */
  dconf_changeset_set (b, "/value/a", g_variant_new_int32 (1));
  g_assert (dconf_changeset_is_similar_to (a, b));
  g_assert (dconf_changeset_is_similar_to (b, a));

  /* make sure even a NULL is counted as different */
  dconf_changeset_set (a, "/value/b", NULL);
  g_assert (!dconf_changeset_is_similar_to (a, b));
  g_assert (!dconf_changeset_is_similar_to (b, a));

  dconf_changeset_set (b, "/value/b", NULL);
  g_assert (dconf_changeset_is_similar_to (a, b));
  g_assert (dconf_changeset_is_similar_to (b, a));

  /* different types are still the same */
  dconf_changeset_set (b, "/value/a", g_variant_new_uint32 (222));
  g_assert (dconf_changeset_is_similar_to (a, b));
  g_assert (dconf_changeset_is_similar_to (b, a));

  dconf_changeset_set (a, "/value/c", NULL);
  dconf_changeset_set (b, "/value/d", NULL);
  g_assert (!dconf_changeset_is_similar_to (a, b));
  g_assert (!dconf_changeset_is_similar_to (b, a));

  dconf_changeset_unref (a);
  dconf_changeset_unref (b);
}

static void
test_describe (void)
{
  DConfChangeset *changeset;
  const gchar * const *keys;
  GVariant * const *values;
  const gchar *prefix;
  gint n_items;
  gint i;

  /* test zero items */
  changeset = dconf_changeset_new ();
  n_items = dconf_changeset_describe (changeset, &prefix, &keys, &values);
  g_assert_cmpint (n_items, ==, 0);
  dconf_changeset_unref (changeset);

  /* test one NULL item */
  changeset = dconf_changeset_new_write ("/value/a", NULL);
  n_items = dconf_changeset_describe (changeset, &prefix, &keys, &values);
  g_assert_cmpint (n_items, ==, 1);
  g_assert_cmpstr (prefix, ==, "/value/a");
  g_assert_cmpstr (keys[0], ==, "");
  g_assert (keys[1] == NULL);
  g_assert (values[0] == NULL);


  /* Check again */
  prefix = NULL;
  keys = NULL;
  values = NULL;
  n_items = dconf_changeset_describe (changeset, &prefix, &keys, &values);
  g_assert_cmpint (n_items, ==, 1);
  g_assert_cmpstr (prefix, ==, "/value/a");
  g_assert_cmpstr (keys[0], ==, "");
  g_assert (keys[1] == NULL);
  g_assert (values[0] == NULL);
  dconf_changeset_unref (changeset);

  /* test one non-NULL item */
  changeset = dconf_changeset_new_write ("/value/a", g_variant_new_int32 (55));
  n_items = dconf_changeset_describe (changeset, &prefix, &keys, &values);
  g_assert_cmpint (n_items, ==, 1);
  g_assert_cmpstr (prefix, ==, "/value/a");
  g_assert_cmpstr (keys[0], ==, "");
  g_assert (keys[1] == NULL);
  g_assert_cmpint (g_variant_get_int32 (values[0]), ==, 55);
  dconf_changeset_unref (changeset);

  /* test many items */
  changeset = dconf_changeset_new ();
  for (i = 0; i < 100; i++)
    {
      gchar key[80];

      g_snprintf (key, sizeof key, "/test/value/%2d", i);

      dconf_changeset_set (changeset, key, g_variant_new_int32 (i));
    }

  n_items = dconf_changeset_describe (changeset, &prefix, &keys, &values);
  g_assert_cmpint (n_items, ==, i);
  g_assert_cmpstr (prefix, ==, "/test/value/");
  for (i = 0; i < 100; i++)
    {
      gchar key[80];

      g_snprintf (key, sizeof key, "%2d", i);

      g_assert_cmpstr (keys[i], ==, key);
      g_assert_cmpint (g_variant_get_int32 (values[i]), ==, i);
    }
  g_assert (keys[n_items] == NULL);
  dconf_changeset_unref (changeset);

  /* test many items with common names */
  changeset = dconf_changeset_new ();
  for (i = 0; i < 100; i++)
    {
      gchar key[80];

      g_snprintf (key, sizeof key, "/test/value/aaa%02d", i);

      dconf_changeset_set (changeset, key, g_variant_new_int32 (i));
    }

  n_items = dconf_changeset_describe (changeset, &prefix, &keys, &values);
  g_assert_cmpint (n_items, ==, i);
  g_assert_cmpstr (prefix, ==, "/test/value/");
  for (i = 0; i < 100; i++)
    {
      gchar key[80];

      g_snprintf (key, sizeof key, "aaa%02d", i);

      g_assert_cmpstr (keys[i], ==, key);
      g_assert_cmpint (g_variant_get_int32 (values[i]), ==, i);
    }
  g_assert (keys[n_items] == NULL);
  dconf_changeset_unref (changeset);

  /* test several values in different directories */
  changeset = dconf_changeset_new ();
  dconf_changeset_set (changeset, "/value/reset/", NULL);
  dconf_changeset_set (changeset, "/value/int/a", g_variant_new_int32 (123));
  dconf_changeset_set (changeset, "/value/string", g_variant_new_string ("bar"));
  dconf_changeset_set (changeset, "/value/string/a", g_variant_new_string ("foo"));
  n_items = dconf_changeset_describe (changeset, &prefix, &keys, &values);
  g_assert_cmpint (n_items, ==, 4);
  g_assert_cmpstr (prefix, ==, "/value/");
  g_assert_cmpstr (keys[0], ==, "int/a");
  g_assert_cmpint (g_variant_get_int32 (values[0]), ==, 123);
  g_assert_cmpstr (keys[1], ==, "reset/");
  g_assert (values[1] == NULL);
  g_assert_cmpstr (keys[2], ==, "string");
  g_assert_cmpstr (g_variant_get_string (values[2], NULL), ==, "bar");
  g_assert_cmpstr (keys[3], ==, "string/a");
  g_assert_cmpstr (g_variant_get_string (values[3], NULL), ==, "foo");
  g_assert (keys[4] == NULL);
  dconf_changeset_unref (changeset);

  /* test a couple of values in very different directories */
  changeset = dconf_changeset_new_write ("/a/deep/directory/", NULL);
  dconf_changeset_set (changeset, "/another/deep/directory/", NULL);
  n_items = dconf_changeset_describe (changeset, &prefix, &keys, &values);
  g_assert_cmpint (n_items, ==, 2);
  g_assert_cmpstr (prefix, ==, "/");
  g_assert_cmpstr (keys[0], ==, "a/deep/directory/");
  g_assert_cmpstr (keys[1], ==, "another/deep/directory/");
  g_assert (keys[2] == NULL);
  g_assert (values[0] == NULL);
  g_assert (values[1] == NULL);
  dconf_changeset_unref (changeset);

  /* one more similar case, but with the first letter different */
  changeset = dconf_changeset_new_write ("/deep/directory/", NULL);
  dconf_changeset_set (changeset, "/another/deep/directory/", NULL);
  n_items = dconf_changeset_describe (changeset, &prefix, &keys, &values);
  g_assert_cmpint (n_items, ==, 2);
  g_assert_cmpstr (prefix, ==, "/");
  g_assert_cmpstr (keys[0], ==, "another/deep/directory/");
  g_assert_cmpstr (keys[1], ==, "deep/directory/");
  g_assert (keys[2] == NULL);
  g_assert (values[0] == NULL);
  g_assert (values[1] == NULL);
  dconf_changeset_unref (changeset);
}

static void
test_reset (void)
{
  DConfChangeset *changeset;

  changeset = dconf_changeset_new ();
  g_assert (!dconf_changeset_get (changeset, "/value/a", NULL));

  /* set a value */
  dconf_changeset_set (changeset, "/value/a", g_variant_new_boolean (TRUE));
  g_assert (dconf_changeset_get (changeset, "/value/a", NULL));

  /* record the reset */
  dconf_changeset_set (changeset, "/value/", NULL);
  g_assert (!dconf_changeset_get (changeset, "/value/a", NULL));

  /* write it back */
  dconf_changeset_set (changeset, "/value/a", g_variant_new_boolean (TRUE));
  g_assert (dconf_changeset_get (changeset, "/value/a", NULL));

  /* reset again */
  dconf_changeset_set (changeset, "/value/", NULL);
  g_assert (!dconf_changeset_get (changeset, "/value/a", NULL));

  /* write again */
  dconf_changeset_set (changeset, "/value/a", g_variant_new_boolean (TRUE));
  g_assert (dconf_changeset_get (changeset, "/value/a", NULL));

  /* reset a different way */
  dconf_changeset_set (changeset, "/value/a", g_variant_new_boolean (TRUE));
  g_assert (dconf_changeset_get (changeset, "/value/a", NULL));

  /* write last time */
  dconf_changeset_set (changeset, "/value/a", g_variant_new_boolean (TRUE));
  g_assert (dconf_changeset_get (changeset, "/value/a", NULL));

  dconf_changeset_unref (changeset);
}

static gboolean
has_same_value (const gchar *key,
                GVariant    *value,
                gpointer     user_data)
{
  DConfChangeset *other = user_data;
  GVariant *other_value;
  gboolean success;

  success = dconf_changeset_get (other, key, &other_value);
  g_assert (success);

  if (value == NULL)
    g_assert (other_value == NULL);
  else
    {
      g_assert (g_variant_equal (value, other_value));
      g_variant_unref (other_value);
    }

  return TRUE;
}

static void
test_serialisation (DConfChangeset *changes)
{
  GVariant *serialised;
  DConfChangeset *copy;

  serialised = dconf_changeset_serialise (changes);
  copy = dconf_changeset_deserialise (serialised);
  g_variant_unref (serialised);

  g_assert (dconf_changeset_is_similar_to (copy, changes));
  g_assert (dconf_changeset_is_similar_to (changes, copy));
  g_assert (dconf_changeset_all (copy, has_same_value, changes));
  g_assert (dconf_changeset_all (changes, has_same_value, copy));

  dconf_changeset_unref (copy);
}

static void
test_serialiser (void)
{
  DConfChangeset *changeset;

  changeset = dconf_changeset_new ();
  test_serialisation (changeset);

  dconf_changeset_set (changeset, "/some/value", g_variant_new_int32 (333));
  test_serialisation (changeset);

  dconf_changeset_set (changeset, "/other/value", NULL);
  test_serialisation (changeset);

  dconf_changeset_set (changeset, "/other/value", g_variant_new_int32 (55));
  test_serialisation (changeset);

  dconf_changeset_set (changeset, "/other/", NULL);
  test_serialisation (changeset);

  dconf_changeset_set (changeset, "/", NULL);
  test_serialisation (changeset);

  dconf_changeset_unref (changeset);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/changeset/basic", test_basic);
  g_test_add_func ("/changeset/similarity", test_similarity);
  g_test_add_func ("/changeset/describe", test_describe);
  g_test_add_func ("/changeset/reset", test_reset);
  g_test_add_func ("/changeset/serialiser", test_serialiser);

  return g_test_run ();
}