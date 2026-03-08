package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.registry.Registry;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestGatewayServicePollerPortedTest {
    private static final Integer CLIENT_TAG = Integer.valueOf(101);
    private static final Integer SERVER_TAG = Integer.valueOf(202);
    private static final Integer GATEWAY_TAG = Integer.valueOf(303);

    @Test
    public void testGatewayReceiverRoundTripViaServicePoller() {
        TestSupport.assumeNative();

        String suffix = Long.toUnsignedString(System.nanoTime(), 36);
        String regPub = TestSupport.inprocEndpoint("gw-sp-pub-" + suffix);
        String regRouter = TestSupport.inprocEndpoint("gw-sp-router-" + suffix);
        String serverEndpoint = TestSupport.tcpEndpoint();
        String clientEndpoint = TestSupport.tcpEndpoint();

        try (Context ctx = new Context();
             Registry registry = new Registry(ctx);
             Discovery clientDiscovery = new Discovery(ctx, ServiceType.GATEWAY);
             Discovery serverDiscovery = new Discovery(ctx, ServiceType.GATEWAY);
             Receiver serverReceiver = new Receiver(ctx);
             Receiver clientReceiver = new Receiver(ctx, "c0");
             Gateway clientGateway = new Gateway(ctx, clientDiscovery, "c0");
             Gateway serverGateway = new Gateway(ctx, serverDiscovery, "sg");
             Poller clientPoller = new Poller();
             Poller serverPoller = new Poller();
             Poller gatewayPoller = new Poller()) {
            registry.setEndpoints(regPub, regRouter);
            registry.start();
            TestSupport.sleepMs(100);

            clientDiscovery.connectRegistry(regPub);
            serverDiscovery.connectRegistry(regPub);
            clientDiscovery.subscribe("perf-server");
            serverDiscovery.subscribe("c0");

            serverReceiver.bind(serverEndpoint);
            serverReceiver.connectRegistry(regRouter);
            serverReceiver.register("perf-server", serverEndpoint, 1);
            clientReceiver.bind(clientEndpoint);
            clientReceiver.connectRegistry(regRouter);
            clientReceiver.register("c0", clientEndpoint, 1);

            TestSupport.waitUntil(
                () -> serverReceiver.registerResult("perf-server").status() == 0,
                TestSupport.DEFAULT_TIMEOUT_MS,
                "server receiver register did not succeed");
            TestSupport.waitUntil(
                () -> clientReceiver.registerResult("c0").status() == 0,
                TestSupport.DEFAULT_TIMEOUT_MS,
                "client receiver register did not succeed");
            TestSupport.waitUntil(
                () -> clientGateway.connectionCount("perf-server") > 0,
                TestSupport.DEFAULT_TIMEOUT_MS,
                "client gateway did not connect to perf-server");
            TestSupport.waitUntil(
                () -> serverGateway.connectionCount("c0") > 0,
                TestSupport.DEFAULT_TIMEOUT_MS,
                "server gateway did not connect to c0");

            clientPoller.addReceiver(clientReceiver,
                PollEventType.POLLIN.getValue(), CLIENT_TAG);
            serverPoller.addReceiver(serverReceiver,
                PollEventType.POLLIN.getValue(), SERVER_TAG);
            gatewayPoller.addGateway(serverGateway,
                PollEventType.POLLOUT.getValue(), GATEWAY_TAG);
            TestSupport.waitUntil(() -> gatewayPoller.pollCount(50) > 0,
                TestSupport.DEFAULT_TIMEOUT_MS,
                "server gateway poller never became writable");
            assertEquals(GATEWAY_TAG, gatewayPoller.readyTag(0));

            try (Message outbound = Message.fromBytes(
                "hello".getBytes(StandardCharsets.UTF_8));
                 Socket serverRouter = serverReceiver.routerSocket();
                 Socket clientRouter = clientReceiver.routerSocket()) {
                clientGateway.sendTo("perf-server", outbound, SendFlag.DONTWAIT);

                TestSupport.waitUntil(() -> serverPoller.pollCount(50) > 0,
                    TestSupport.DEFAULT_TIMEOUT_MS,
                    "server receiver poller never became readable");
                assertEquals(SERVER_TAG, serverPoller.readyTag(0));
                byte[] serverRid = TestSupport.recvWithTimeout(serverRouter, 256,
                    TestSupport.DEFAULT_TIMEOUT_MS);
                byte[] serverPayload = TestSupport.recvWithTimeout(serverRouter, 256,
                    TestSupport.DEFAULT_TIMEOUT_MS);
                while (serverRouter.getSockOptInt(SocketOption.RCVMORE) != 0) {
                    serverPayload = TestSupport.recvWithTimeout(serverRouter, 256,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                }
                assertEquals("c0",
                    new String(serverRid, StandardCharsets.UTF_8));
                assertEquals("hello",
                    new String(serverPayload, StandardCharsets.UTF_8));

                try (Message echo = Message.fromBytes(serverPayload)) {
                    serverGateway.sendTo("c0", echo, SendFlag.DONTWAIT);
                }

                TestSupport.waitUntil(() -> clientPoller.pollCount(50) > 0,
                    TestSupport.DEFAULT_TIMEOUT_MS,
                    "client receiver poller never became readable");
                assertEquals(CLIENT_TAG, clientPoller.readyTag(0));
                byte[] clientRid = TestSupport.recvWithTimeout(clientRouter, 256,
                    TestSupport.DEFAULT_TIMEOUT_MS);
                byte[] clientPayload = TestSupport.recvWithTimeout(clientRouter, 256,
                    TestSupport.DEFAULT_TIMEOUT_MS);
                while (clientRouter.getSockOptInt(SocketOption.RCVMORE) != 0) {
                    clientPayload = TestSupport.recvWithTimeout(clientRouter, 256,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                }
                assertTrue(clientRid.length > 0);
                assertEquals("hello",
                    new String(clientPayload, StandardCharsets.UTF_8));
            }
        }
    }

    @Test
    public void testGatewayReceiverRoundTripViaServicePollerAcrossContexts() {
        TestSupport.assumeNative();

        String suffix = Long.toUnsignedString(System.nanoTime(), 36);
        String regPub = TestSupport.tcpEndpoint();
        String regRouter = TestSupport.tcpEndpoint();
        String serverEndpoint = TestSupport.tcpEndpoint();
        String clientEndpoint = TestSupport.tcpEndpoint();

        try (Context registryCtx = new Context();
             Context serverCtx = new Context();
             Context clientCtx = new Context();
             Registry registry = new Registry(registryCtx);
             Discovery clientDiscovery = new Discovery(clientCtx, ServiceType.GATEWAY);
             Discovery serverDiscovery = new Discovery(serverCtx, ServiceType.GATEWAY);
             Receiver serverReceiver = new Receiver(serverCtx);
             Receiver clientReceiver = new Receiver(clientCtx, "c0");
             Gateway clientGateway = new Gateway(clientCtx, clientDiscovery, "c0");
             Gateway serverGateway = new Gateway(serverCtx, serverDiscovery, "sg");
             Poller clientPoller = new Poller();
             Poller serverPoller = new Poller();
             Poller gatewayPoller = new Poller()) {
            registry.setEndpoints(regPub, regRouter);
            registry.start();
            TestSupport.sleepMs(100);

            clientDiscovery.connectRegistry(regPub);
            serverDiscovery.connectRegistry(regPub);
            clientDiscovery.subscribe("perf-server");
            serverDiscovery.subscribe("c0");

            serverReceiver.bind(serverEndpoint);
            serverReceiver.connectRegistry(regRouter);
            serverReceiver.register("perf-server", serverEndpoint, 1);
            clientReceiver.bind(clientEndpoint);
            clientReceiver.connectRegistry(regRouter);
            clientReceiver.register("c0", clientEndpoint, 1);

            TestSupport.waitUntil(
                () -> serverReceiver.registerResult("perf-server").status() == 0,
                TestSupport.DEFAULT_TIMEOUT_MS,
                "server receiver register did not succeed");
            TestSupport.waitUntil(
                () -> clientReceiver.registerResult("c0").status() == 0,
                TestSupport.DEFAULT_TIMEOUT_MS,
                "client receiver register did not succeed");
            TestSupport.waitUntil(
                () -> clientGateway.connectionCount("perf-server") > 0,
                TestSupport.DEFAULT_TIMEOUT_MS,
                "client gateway did not connect to perf-server");
            TestSupport.waitUntil(
                () -> serverGateway.connectionCount("c0") > 0,
                TestSupport.DEFAULT_TIMEOUT_MS,
                "server gateway did not connect to c0");

            clientPoller.addReceiver(clientReceiver,
                PollEventType.POLLIN.getValue(), CLIENT_TAG);
            serverPoller.addReceiver(serverReceiver,
                PollEventType.POLLIN.getValue(), SERVER_TAG);
            gatewayPoller.addGateway(serverGateway,
                PollEventType.POLLOUT.getValue(), GATEWAY_TAG);
            TestSupport.waitUntil(() -> gatewayPoller.pollCount(50) > 0,
                TestSupport.DEFAULT_TIMEOUT_MS,
                "server gateway poller never became writable");
            assertEquals(GATEWAY_TAG, gatewayPoller.readyTag(0));

            try (Message outbound = Message.fromBytes(
                ("hello-" + suffix).getBytes(StandardCharsets.UTF_8));
                 Socket serverRouter = serverReceiver.routerSocket();
                 Socket clientRouter = clientReceiver.routerSocket()) {
                clientGateway.sendTo("perf-server", outbound, SendFlag.DONTWAIT);

                TestSupport.waitUntil(() -> serverPoller.pollCount(50) > 0,
                    TestSupport.DEFAULT_TIMEOUT_MS,
                    "server receiver poller never became readable");
                assertEquals(SERVER_TAG, serverPoller.readyTag(0));
                byte[] serverRid = TestSupport.recvWithTimeout(serverRouter, 256,
                    TestSupport.DEFAULT_TIMEOUT_MS);
                byte[] serverPayload = TestSupport.recvWithTimeout(serverRouter, 256,
                    TestSupport.DEFAULT_TIMEOUT_MS);
                while (serverRouter.getSockOptInt(SocketOption.RCVMORE) != 0) {
                    serverPayload = TestSupport.recvWithTimeout(serverRouter, 256,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                }
                assertEquals("c0",
                    new String(serverRid, StandardCharsets.UTF_8));
                assertEquals("hello-" + suffix,
                    new String(serverPayload, StandardCharsets.UTF_8));

                try (Message echo = Message.fromBytes(serverPayload)) {
                    serverGateway.sendTo("c0", echo, SendFlag.DONTWAIT);
                }

                TestSupport.waitUntil(() -> clientPoller.pollCount(50) > 0,
                    TestSupport.DEFAULT_TIMEOUT_MS,
                    "client receiver poller never became readable");
                assertEquals(CLIENT_TAG, clientPoller.readyTag(0));
                byte[] clientRid = TestSupport.recvWithTimeout(clientRouter, 256,
                    TestSupport.DEFAULT_TIMEOUT_MS);
                byte[] clientPayload = TestSupport.recvWithTimeout(clientRouter, 256,
                    TestSupport.DEFAULT_TIMEOUT_MS);
                while (clientRouter.getSockOptInt(SocketOption.RCVMORE) != 0) {
                    clientPayload = TestSupport.recvWithTimeout(clientRouter, 256,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                }
                assertTrue(clientRid.length > 0);
                assertEquals("hello-" + suffix,
                    new String(clientPayload, StandardCharsets.UTF_8));
            }
        }
    }
}
