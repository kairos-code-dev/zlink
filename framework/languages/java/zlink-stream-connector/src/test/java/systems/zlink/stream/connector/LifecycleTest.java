package systems.zlink.stream.connector;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;

final class LifecycleTest {
    @Test
    void connectorStartsInCreatedStateBeforeFirstConnectAttempt() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                ZLinkStreamConnectorFactory.create(server.options(ZLinkStreamDispatchMode.MANUAL));
            try {
                assertEquals(ZLinkStreamConnectionState.CREATED, connector.state());
                assertFalse(connector.isConnected());
            } finally {
                ConnectorTestAwait.await(connector.close());
            }
        }
    }

    @Test
    void requestTimeoutFailsPendingRequestsWithTimeoutCause() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                ZLinkStreamConnectorFactory.create(server.options(ZLinkStreamDispatchMode.MANUAL));
            try {
            ConnectorTestAwait.await(connector.connect());

            CompletionException ex = assertThrows(CompletionException.class, () ->
                connector.request(payload("MissingReply", "hello"))
                    .timeout(Duration.ofMillis(10))
                    .submit()
                    .toCompletableFuture()
                    .join());

            assertTrue(ex.getCause() instanceof java.util.concurrent.TimeoutException);
            assertEquals(0, connector.pendingDispatchCount());
            } finally {
                ConnectorTestAwait.await(connector.close());
            }
        }
    }

    @Test
    void reconnectRestoresConnectionAfterTransportClose() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                ZLinkStreamConnectorFactory.create(server.options(ZLinkStreamDispatchMode.AUTO));
            try {
            List<ZLinkStreamConnectionState> states = new ArrayList<>();
            connector.onConnectionStateChanged(state -> {
                states.add(state);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            ConnectorTestAwait.await(connector.connect());
            ConnectorTestAwait.await(connector.disconnect());
            assertFalse(connector.isConnected());

            ConnectorTestAwait.await(connector.reconnect());

            assertTrue(connector.isConnected());
            assertEquals(List.of(
                ZLinkStreamConnectionState.CONNECTING,
                ZLinkStreamConnectionState.CONNECTED,
                ZLinkStreamConnectionState.DISCONNECTED,
                ZLinkStreamConnectionState.RECONNECTING,
                ZLinkStreamConnectionState.CONNECTED), states);
            } finally {
                ConnectorTestAwait.await(connector.close());
            }
        }
    }

    @Test
    void heartbeatSendsReservedControlPing() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(server.options(
                 ZLinkStreamDispatchMode.AUTO,
                 Duration.ofSeconds(1),
                 1,
                 true,
                 Duration.ofMillis(25),
                 Duration.ofMillis(500),
                 Duration.ofMillis(10)));
            try {
            ConnectorTestAwait.await(connector.connect());

            TcpStreamConnectorTestServer.ReceivedFrame frame =
                server.readFrameAsync().get(1, TimeUnit.SECONDS);

            assertEquals(ZLinkStreamWireProtocol.KIND_CONTROL, frame.header().kind());
            assertEquals("$zlink.heartbeat.ping", frame.header().name());
            assertEquals(0, frame.payload().length);
            } finally {
                ConnectorTestAwait.await(connector.close());
            }
        }
    }

    @Test
    void inboundHeartbeatPingReceivesPongWhenHeartbeatDisabled() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(server.options(
                 ZLinkStreamDispatchMode.AUTO,
                 Duration.ofSeconds(1),
                 1,
                 false,
                 Duration.ofMillis(25),
                 Duration.ofMillis(500),
                 Duration.ofMillis(10)));
            try {
            ConnectorTestAwait.await(connector.connect());
            server.sendAsync(control("$zlink.heartbeat.ping"), new byte[0]).join();

            TcpStreamConnectorTestServer.ReceivedFrame frame =
                server.readFrameAsync().get(1, TimeUnit.SECONDS);

            assertEquals(ZLinkStreamWireProtocol.KIND_CONTROL, frame.header().kind());
            assertEquals("$zlink.heartbeat.pong", frame.header().name());
            assertEquals(0, frame.payload().length);
            } finally {
                ConnectorTestAwait.await(connector.close());
            }
        }
    }

    @Test
    void heartbeatTimeoutFailsPendingRequestsWithTimeoutCause() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(server.options(
                 ZLinkStreamDispatchMode.AUTO,
                 Duration.ofSeconds(5),
                 1,
                 true,
                 Duration.ofMillis(20),
                 Duration.ofMillis(60),
                 Duration.ofMillis(10)));
            try {
            ConnectorTestAwait.await(connector.connect());

            CompletionException ex = assertThrows(CompletionException.class, () ->
                connector.request(payload("MissingReply", "hello"))
                    .submit()
                    .toCompletableFuture()
                    .join());

            assertTrue(ex.getCause() instanceof java.util.concurrent.TimeoutException);
            assertTrue(ex.getCause().getMessage().contains("Heartbeat"));
            } finally {
                ConnectorTestAwait.await(connector.close());
            }
        }
    }

    @Test
    void reconnectFailsAfterMaxAttemptsWhenEndpointUnavailable() throws Exception {
        ZLinkStreamConnectorOptions options = new ZLinkStreamConnectorOptions(
            java.net.URI.create("tcp://127.0.0.1:1"),
            ZLinkStreamDispatchMode.AUTO,
            Duration.ofMillis(100),
            2,
            Duration.ofMillis(100),
            64 * 1024,
            false,
            Duration.ofMillis(25),
            Duration.ofMillis(100),
            true,
            Duration.ofMillis(10),
            Duration.ofMillis(20),
            2.0);
        ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(options);
        try {
            List<ZLinkStreamConnectionState> states = new ArrayList<>();
            connector.onConnectionStateChanged(state -> {
                states.add(state);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            assertThrows(Exception.class, () ->
                ConnectorTestAwait.await(connector.reconnect()));

            assertEquals(List.of(
                ZLinkStreamConnectionState.RECONNECTING,
                ZLinkStreamConnectionState.DISCONNECTED), states);
        } finally {
            ConnectorTestAwait.await(connector.close());
        }
    }

    @Test
    void reconnectUnlimitedAttemptsRestoresLateServer() throws Exception {
        int port = reservePort();
        CompletableFuture<Void> accepted = new CompletableFuture<>();
        CompletableFuture<Void> release = new CompletableFuture<>();
        CompletableFuture<Void> server = CompletableFuture.runAsync(() -> {
            try (ServerSocket listener = new ServerSocket()) {
                listener.setReuseAddress(true);
                listener.bind(new InetSocketAddress("127.0.0.1", port));
                listener.setSoTimeout((int) Duration.ofSeconds(5).toMillis());
                try (var ignored = listener.accept()) {
                    accepted.complete(null);
                    release.get(5, TimeUnit.SECONDS);
                }
            } catch (Exception ex) {
                accepted.completeExceptionally(ex);
                throw new RuntimeException(ex);
            }
        }, CompletableFuture.delayedExecutor(120, TimeUnit.MILLISECONDS));
        ZLinkStreamConnectorOptions options = new ZLinkStreamConnectorOptions(
            java.net.URI.create("tcp://127.0.0.1:" + port),
            ZLinkStreamDispatchMode.AUTO,
            Duration.ofMillis(100),
            ZLinkStreamConnectorOptions.UNLIMITED_RECONNECT_ATTEMPTS,
            Duration.ofMillis(20),
            64 * 1024,
            false,
            Duration.ofMillis(25),
            Duration.ofMillis(100),
            true,
            Duration.ofMillis(10),
            Duration.ofMillis(10),
            1.0);
        ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(options);
        try {
            connector.reconnect().submit().toCompletableFuture().get(5, TimeUnit.SECONDS);
            accepted.get(5, TimeUnit.SECONDS);

            assertTrue(connector.isConnected());
        } finally {
            ConnectorTestAwait.await(connector.close());
            release.complete(null);
            server.get(5, TimeUnit.SECONDS);
        }
    }

    @Test
    void reconnectEnabledRejectsZeroMaxAttempts() {
        ZLinkStreamConnectorOptions options = new ZLinkStreamConnectorOptions(
            java.net.URI.create("tcp://127.0.0.1:1"),
            ZLinkStreamDispatchMode.AUTO,
            Duration.ofMillis(100),
            0,
            Duration.ofMillis(100),
            64 * 1024,
            false,
            Duration.ofMillis(25),
            Duration.ofMillis(100),
            true,
            Duration.ofMillis(10),
            Duration.ofMillis(20),
            2.0);

        assertThrows(IllegalArgumentException.class, () ->
            ZLinkStreamConnectorFactory.create(options));
    }

    @Test
    void closeWhileReconnectingKeepsConnectorClosed() throws Exception {
        ZLinkStreamConnectorOptions options = new ZLinkStreamConnectorOptions(
            java.net.URI.create("tcp://127.0.0.1:1"),
            ZLinkStreamDispatchMode.AUTO,
            Duration.ofMillis(100),
            2,
            Duration.ofMillis(100),
            64 * 1024,
            false,
            Duration.ofMillis(25),
            Duration.ofMillis(100),
            true,
            Duration.ofMillis(100),
            Duration.ofMillis(100),
            2.0);
        ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(options);
        try {
            var reconnect = connector.reconnect().submit().toCompletableFuture();

            ConnectorTestAwait.await(connector.close());

            CompletionException ex = assertThrows(CompletionException.class, reconnect::join);
            assertTrue(ex.getCause() instanceof IllegalStateException);
            assertEquals(ZLinkStreamConnectionState.CLOSED, connector.state());
            assertFalse(connector.isConnected());
        } finally {
            ConnectorTestAwait.await(connector.close());
        }
    }

    private static ZLinkStreamEncodedPayload payload(String packetName, String body) {
        return new ZLinkStreamEncodedPayload(
            packetName,
            Message.from(body),
            Map.of());
    }

    private static ZLinkStreamWireProtocol.Header control(String name) {
        return new ZLinkStreamWireProtocol.Header(
            ZLinkStreamWireProtocol.KIND_CONTROL,
            ZLinkStreamWireProtocol.CODEC_RAW,
            0,
            null,
            name,
            Map.of(),
            null);
    }

    private static int reservePort() throws Exception {
        try (ServerSocket socket = new ServerSocket(0)) {
            return socket.getLocalPort();
        }
    }
}
