package net.retropc.pi;

import static org.junit.Assert.assertEquals;

import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import org.junit.Test;

public class RaHttpHeadersTest {
    private static final class FakeConnection extends HttpURLConnection {
        FakeConnection() throws IOException {
            super(new URL("https://example.invalid"));
        }

        @Override public void disconnect() {
        }

        @Override public boolean usingProxy() {
            return false;
        }

        @Override public void connect() {
        }
    }

    @Test
    public void appliesCallerSuppliedUserAgentExactly() throws Exception {
        HttpURLConnection connection = new FakeConnection();

        RaHttpHeaders.apply(connection, "XM8M/2.0.0");

        assertEquals("XM8M/2.0.0", connection.getRequestProperty("User-Agent"));
    }
}
