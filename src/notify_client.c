#include "notify.h"
#include <gio/gio.h>

int main(int argc, char **argv)
{
    if (argc != 2 || !g_utf8_validate(argv[1], -1, NULL)) {
        g_printerr("Usage: %s 'UTF-8 message'\n", argv[0]);
        return 2;
    }
    GError *error = NULL;
    GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!connection) {
        g_printerr("Connect to session bus: %s\n", error->message);
        g_error_free(error);
        return 1;
    }
    GVariant *reply = g_dbus_connection_call_sync(
        connection, SERVICE, OBJECT_PATH, INTERFACE, "Notify",
        g_variant_new("(s)", argv[1]), G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NO_AUTO_START, 5000, NULL, &error);
    g_object_unref(connection);

    if (!reply) {
        g_printerr("Notify: %s\n", error->message);
        g_error_free(error);
        return 1;
    }
    gboolean received;
    g_variant_get(reply, "(b)", &received);
    g_variant_unref(reply);
    g_print("Server reply: %s\n", received ? "true" : "false");
    return received ? 0 : 1;
}
