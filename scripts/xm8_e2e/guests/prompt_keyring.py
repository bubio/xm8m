#!/usr/bin/env python3
"""Display the Linux Secret Service prompt; never read or enter a secret."""
import sys


def main(timeout):
    from gi.repository import Gio, GLib
    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    service = "org.freedesktop.secrets"
    collection = "/org/freedesktop/secrets/aliases/default"

    def call(path, interface, method, parameters, signature):
        return connection.call_sync(service, path, interface, method, parameters,
            GLib.VariantType.new(signature), Gio.DBusCallFlags.NONE, 5000, None)

    def locked():
        result = call(collection, "org.freedesktop.DBus.Properties", "Get",
            GLib.Variant("(ss)", ("org.freedesktop.Secret.Collection", "Locked")), "(v)")
        return result.get_child_value(0).get_variant().get_boolean()

    if not locked():
        return 0
    result = call("/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "Unlock",
        GLib.Variant("(ao)", ([collection],)), "(aoo)")
    prompt = result.get_child_value(1).get_string()
    if prompt == "/":
        return 0 if not locked() else 3
    loop = GLib.MainLoop()
    outcome = [4]

    def completed(_connection, _sender, _path, _interface, _signal, parameters, _data):
        dismissed = parameters.get_child_value(0).get_boolean()
        outcome[0] = 2 if dismissed else 0 if not locked() else 3
        loop.quit()

    connection.signal_subscribe(service, "org.freedesktop.Secret.Prompt", "Completed", prompt,
        None, Gio.DBusSignalFlags.NONE, completed, None)
    # Keep this same bus connection alive: prompt objects belong to their caller.
    call(prompt, "org.freedesktop.Secret.Prompt", "Prompt", GLib.Variant("(s)", ("",)), "()")
    GLib.timeout_add_seconds(timeout, lambda: (loop.quit(), False)[1])
    loop.run()
    return outcome[0]


if __name__ == "__main__":
    try:
        duration = int(sys.argv[1])
        if not 30 <= duration <= 900:
            raise ValueError("invalid timeout")
        status = main(duration)
    except Exception:
        print("Keyring prompt is unavailable", file=sys.stderr)
        status = 3
    raise SystemExit(status)
