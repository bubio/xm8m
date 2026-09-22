package net.retropc.pi;

import java.net.HttpURLConnection;

final class RaHttpHeaders {
    private RaHttpHeaders() {
    }

    static void apply(HttpURLConnection connection, String userAgent) {
        if (connection == null) {
            throw new IllegalArgumentException("connection is required");
        }
        if (userAgent == null || userAgent.isEmpty()) {
            throw new IllegalArgumentException("User-Agent is required");
        }
        connection.setRequestProperty("User-Agent", userAgent);
    }
}
