package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Assumptions;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.net.ServerSocket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestZmpWsWssPortedTest {
    @Test
    public void testZmpWsPairMessage() {
        IntegrationSupport.requireCapability("ws");
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.PAIR);
             Socket client = new Socket(ctx, SocketType.PAIR)) {
            server.setSockOpt(SocketOption.LINGER, 0);
            client.setSockOpt(SocketOption.LINGER, 0);

            String endpoint = TestSupport.wsEndpoint();
            server.bind(endpoint);
            client.connect(endpoint);
            TestSupport.sleepMs(80);

            client.send("ws-zmp".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            assertEquals("ws-zmp", new String(TestSupport.recvWithTimeout(server,
                128, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
        }
    }

    @Test
    public void testZmpWssPairMessage() {
        IntegrationSupport.requireCapability("wss");
        TestSupport.assumeNative();

        Path serverCert = coreTestCert("server.crt");
        Path serverKey = coreTestCert("server.key");
        Path caCert = coreTestCert("ca.crt");
        Assumptions.assumeTrue(Files.isRegularFile(serverCert)
            && Files.isRegularFile(serverKey)
            && Files.isRegularFile(caCert),
            "core test certs not found");

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.PAIR);
             Socket client = new Socket(ctx, SocketType.PAIR)) {
            server.setSockOpt(SocketOption.LINGER, 0);
            client.setSockOpt(SocketOption.LINGER, 0);
            client.setSockOpt(SocketOption.TLS_TRUST_SYSTEM, 0);

            server.setSockOpt(SocketOption.TLS_CERT,
                serverCert.toString().getBytes(StandardCharsets.UTF_8));
            server.setSockOpt(SocketOption.TLS_KEY,
                serverKey.toString().getBytes(StandardCharsets.UTF_8));
            client.setSockOpt(SocketOption.TLS_CA,
                caCert.toString().getBytes(StandardCharsets.UTF_8));
            client.setSockOpt(SocketOption.TLS_HOSTNAME,
                "localhost".getBytes(StandardCharsets.UTF_8));

            String endpoint = "wss://127.0.0.1:" + randomPort();
            server.bind(endpoint);
            client.connect(endpoint);
            TestSupport.sleepMs(100);

            client.send("wss-zmp".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            assertEquals("wss-zmp", new String(TestSupport.recvWithTimeout(server,
                128, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
        }
    }

    private static Path coreTestCert(String name) {
        return Path.of(System.getProperty("user.dir"))
            .resolve("../../core/tests/certs/gen/" + name)
            .normalize()
            .toAbsolutePath();
    }

    private static int randomPort() {
        try (ServerSocket socket = new ServerSocket(0)) {
            socket.setReuseAddress(true);
            return socket.getLocalPort();
        } catch (IOException ex) {
            throw new RuntimeException(ex);
        }
    }
}
