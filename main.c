#include "glib-object.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <pipewire/pipewire.h>
#include <stdio.h>
#include <wp/wp.h>

static struct {
  GMainLoop *loop;
  WpCore *core;
  WpObjectManager *om;
  gint streams;
  gint interval;
  gchar **swayidle_argv;
  GSubprocess *swayidle_proc;
} g;

static void check_node(const GValue *item, gpointer data) {
  WpPipewireObject *obj = g_value_get_object(item);
  if (!obj)
    return;

  guint32 id = wp_proxy_get_bound_id(WP_PROXY(obj));
  g_autoptr(WpIterator) it = wp_object_manager_new_filtered_iterator(
      g.om, WP_TYPE_PORT, WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_ID, "=u",
      id, NULL);

  g_auto(GValue) val = G_VALUE_INIT;
  for (; wp_iterator_next(it, &val); g_value_unset(&val)) {
    WpPort *port = g_value_get_object(&val);
    if (!port || wp_port_get_direction(port) != WP_DIRECTION_OUTPUT)
      continue;

    guint32 port_id = wp_proxy_get_bound_id(WP_PROXY(port));
    g_autoptr(WpLink) link = wp_object_manager_lookup(
        g.om, WP_TYPE_LINK, WP_CONSTRAINT_TYPE_PW_PROPERTY,
        PW_KEY_LINK_OUTPUT_PORT, "=u", port_id, NULL);

    if (link) {
      g_autoptr(GEnumClass) klass = g_type_class_ref(WP_TYPE_LINK_STATE);
      GEnumValue *state =
          g_enum_get_value(klass, wp_link_get_state(link, NULL));
      if (state && strcmp(state->value_nick, "active") == 0)
        g.streams++;
    }
  }
}

static void check_streams(void) {
  g.streams = 0;
  const gchar *types[] = {"*Audio*", "*Video*"};

  for (guint i = 0; i < 2; i++) {
    g_autoptr(WpIterator) it = wp_object_manager_new_filtered_iterator(
        g.om, WP_TYPE_NODE, WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_MEDIA_CLASS,
        "#s", "Stream/*", WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_MEDIA_CLASS,
        "#s", types[i], WP_CONSTRAINT_TYPE_PW_PROPERTY, PW_KEY_NODE_LINK_GROUP,
        "-", NULL);
    wp_iterator_foreach(it, check_node, NULL);
  }
}

static void manage_swayidle(void) {
  gboolean running =
      g.swayidle_proc && !g_subprocess_get_if_exited(g.swayidle_proc);

  if (g.streams > 0) {
    if (running) {
      g_subprocess_force_exit(g.swayidle_proc);
      g_clear_object(&g.swayidle_proc);
    }
  } else if (!running) {
    GError *error = NULL;
    g.swayidle_proc = g_subprocess_newv((const gchar *const *)g.swayidle_argv,
                                        G_SUBPROCESS_FLAGS_NONE, &error);
    if (!g.swayidle_proc) {
      g_warning("Failed to start swayidle: %s", error->message);
      g_clear_error(&error);
    }
  }
}

static gboolean periodic_check(gpointer data) {
  check_streams();
  manage_swayidle();
  return TRUE;
}

static void on_plugin_loaded(WpCore *core, GAsyncResult *res, gpointer data) {
  wp_core_install_object_manager(g.core, g.om);
}

static void on_installed(WpObjectManager *om, gpointer data) {
  periodic_check(NULL);
  g_timeout_add_seconds(g.interval, periodic_check, NULL);
}

static gboolean on_signal_int(gpointer data) {
  g_main_loop_quit(g.loop);
  return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <interval> [-- <swayidle args>...]\n",
            argv[0]);
    return 1;
  }

  g.interval = atoi(argv[1]);

  // Parse swayidle args
  int start = -1;
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) {
      start = i + 1;
      break;
    }
  }

  if (start > 0 && start < argc) {
    int count = argc - start;
    g.swayidle_argv = g_new0(gchar *, count + 2);
    g.swayidle_argv[0] = g_strdup("swayidle");
    for (int i = 0; i < count; i++)
      g.swayidle_argv[i + 1] = g_strdup(argv[start + i]);
  } else {
    g.swayidle_argv = g_new0(gchar *, 2);
    g.swayidle_argv[0] = g_strdup("swayidle");
  }

  wp_init(WP_INIT_ALL);

  g.loop = g_main_loop_new(NULL, FALSE);
  g.core = wp_core_new(NULL, NULL, NULL);
  g.om = wp_object_manager_new();

  wp_object_manager_add_interest(g.om, WP_TYPE_NODE, NULL);
  wp_object_manager_add_interest(g.om, WP_TYPE_PORT, NULL);
  wp_object_manager_add_interest(g.om, WP_TYPE_LINK, NULL);
  wp_object_manager_request_object_features(
      g.om, WP_TYPE_GLOBAL_PROXY, WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);

  wp_core_load_component(g.core, "libwireplumber-module-default-nodes-api",
                         "module", NULL, NULL, NULL,
                         (GAsyncReadyCallback)on_plugin_loaded, NULL);

  if (!wp_core_connect(g.core)) {
    fprintf(stderr, "Could not connect to PipeWire\n");
    return 2;
  }

  g_signal_connect(g.om, "installed", G_CALLBACK(on_installed), NULL);
  g_unix_signal_add(SIGINT, on_signal_int, NULL);

  g_main_loop_run(g.loop);

  // Cleanup
  if (g.swayidle_proc && !g_subprocess_get_if_exited(g.swayidle_proc)) {
    g_subprocess_force_exit(g.swayidle_proc);
    g_clear_object(&g.swayidle_proc);
  }
  g_strfreev(g.swayidle_argv);
  g_clear_object(&g.om);
  g_clear_object(&g.core);
  g_main_loop_unref(g.loop);

  return 0;
}
