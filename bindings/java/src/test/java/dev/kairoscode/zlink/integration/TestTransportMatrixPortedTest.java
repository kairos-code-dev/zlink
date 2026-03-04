package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestTransportMatrixPortedTest {
    @Test
    public void testTransportMatrix() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            for (String transport : IntegrationSupport.coreTransports()) {
                runPair(ctx, transport);
                runPubSub(ctx, transport);
                runRouterDealer(ctx, transport);
            }
        }
    }

    private static void runPair(Context ctx, String transport) {
        try (Socket server = new Socket(ctx, SocketType.PAIR);
             Socket client = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = IntegrationSupport.endpointFor(transport,
                "matrix-pair");
            server.bind(endpoint);
            client.connect(endpoint);
            TestSupport.sleepMs(40);

            client.send("pair-hello".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            assertEquals("pair-hello", new String(TestSupport.recvWithTimeout(
                server, 64, TestSupport.DEFAULT_TIMEOUT_MS),
                StandardCharsets.UTF_8));
            server.send("pair-ack".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            assertEquals("pair-ack", new String(TestSupport.recvWithTimeout(
                client, 64, TestSupport.DEFAULT_TIMEOUT_MS),
                StandardCharsets.UTF_8));
        }
    }

    private static void runPubSub(Context ctx, String transport) {
        try (Socket pub = new Socket(ctx, SocketType.PUB);
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            String endpoint = IntegrationSupport.endpointFor(transport,
                "matrix-pubsub");
            pub.bind(endpoint);
            sub.connect(endpoint);
            sub.setSockOpt(SocketOption.SUBSCRIBE, new byte[0]);
            TestSupport.sleepMs(60);

            pub.send("pubsub-hello".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            assertEquals("pubsub-hello", new String(TestSupport.recvWithTimeout(
                sub, 128, TestSupport.DEFAULT_TIMEOUT_MS),
                StandardCharsets.UTF_8));
        }
    }

    private static void runRouterDealer(Context ctx, String transport) {
        try (Socket router = new Socket(ctx, SocketType.ROUTER);
             Socket dealer = new Socket(ctx, SocketType.DEALER)) {
            dealer.setSockOpt(SocketOption.ROUTING_ID,
                "DEALER1".getBytes(StandardCharsets.UTF_8));

            String endpoint = IntegrationSupport.endpointFor(transport,
                "matrix-router-dealer");
            router.bind(endpoint);
            dealer.connect(endpoint);
            TestSupport.sleepMs(60);

            dealer.send("dealer-msg".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            assertEquals("DEALER1", new String(TestSupport.recvWithTimeout(router,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            assertEquals("dealer-msg", new String(TestSupport.recvWithTimeout(
                router, 64, TestSupport.DEFAULT_TIMEOUT_MS),
                StandardCharsets.UTF_8));

            router.send("DEALER1".getBytes(StandardCharsets.UTF_8),
                SendFlag.SNDMORE);
            router.send("router-reply".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            assertEquals("router-reply", new String(TestSupport.recvWithTimeout(
                dealer, 64, TestSupport.DEFAULT_TIMEOUT_MS),
                StandardCharsets.UTF_8));
        }
    }
}
