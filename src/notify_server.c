#include "notify.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include <signal.h>

static const gchar interface_xml[] =
    "<node><interface name='" INTERFACE "'>"
    "<method name='Notify'>"
    "<arg name='message' type='s' direction='in'/>"
    "<arg name='received' type='b' direction='out'/>"
    "</method></interface></node>";

struct server {
    GMainLoop *loop;
    GDBusNodeInfo *description;
    GDBusConnection *connection;
    guint registration;
    int failed;
    guint sigint_watch;
    guint sigterm_watch;
};

static void on_method(GDBusConnection *connection, const gchar *sender,
                      const gchar *path, const gchar *interface, const gchar *method,
                      GVariant *arguments, GDBusMethodInvocation *invocation,
                      gpointer data)
{
    (void)connection;
    (void)sender;
    (void)path;
    (void)interface;
    (void)method;
    (void)data;
    const gchar *message;
    g_variant_get(arguments, "(&s)", &message);
    g_print("Notification: %s\n", message);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
}

static const GDBusInterfaceVTable callbacks = { .method_call = on_method };

static void on_bus(GDBusConnection *connection, const gchar *name, gpointer data)
{
    (void)name;
    struct server *server = data;
    GError *error = NULL;
    server->connection = g_object_ref(connection);
    server->registration = g_dbus_connection_register_object(
        connection, OBJECT_PATH, server->description->interfaces[0],
        &callbacks, NULL, NULL, &error);
    if (!server->registration) {
        g_printerr("Register object: %s\n", error->message);
        g_error_free(error);
        server->failed = 1;
        g_main_loop_quit(server->loop);
    }
}

static void on_name(GDBusConnection *connection, const gchar *name, gpointer data)
{
    (void)connection;
    (void)data;
    g_print("Listening on %s\n", name);
}

static void on_name_lost(GDBusConnection *connection, const gchar *name, gpointer data)
{
    (void)connection;
    struct server *server = data;
    g_printerr("Could not keep the name %s on the session bus.\n", name);
    server->failed = 1;
    g_main_loop_quit(server->loop);
}

static gboolean stop(gpointer data)
{
    struct server *server = data;
    g_main_loop_quit(server->loop);
    return G_SOURCE_CONTINUE;
}

int main(void)
{
    GError *error = NULL;
    struct server server = {0};
    server.description = g_dbus_node_info_new_for_xml(interface_xml, &error);
    if (!server.description) {
        g_printerr("%s\n", error->message);
        g_error_free(error);
        return 1;
    }

    server.loop = g_main_loop_new(NULL, FALSE);
    server.sigint_watch = g_unix_signal_add(SIGINT, stop, &server);
    server.sigterm_watch = g_unix_signal_add(SIGTERM, stop, &server);
    guint owner = g_bus_own_name(G_BUS_TYPE_SESSION, SERVICE,
        G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE, on_bus, on_name, on_name_lost, &server, NULL);
    g_main_loop_run(server.loop);

    g_source_remove(server.sigint_watch);
    g_source_remove(server.sigterm_watch);
    if (server.registration)
        g_dbus_connection_unregister_object(server.connection, server.registration);
    g_bus_unown_name(owner);
    g_clear_object(&server.connection);
    g_main_loop_unref(server.loop);
    g_dbus_node_info_unref(server.description);
    return server.failed;
}
