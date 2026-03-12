package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.registry.Registry;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestGatewayServicePollerPortedTest {
    private static final Integer CLIENT_TAG = Integer.valueOf(101);
    private static final Integer SERVER_TAG = Integer.valueOf(202);
    private static final Integer GATEWAY_TAG = Integer.valueOf(303);

    @Test
    public void testGatewayReceiverRoundTripViaServicePoller() {
        runRoundTrip(new Context(), new Context(), new Context(), true);
    }

    @Test
    public void testGatewayReceiverRoundTripViaServicePollerAcrossContexts() {
        runRoundTrip(new Context(), new Context(), new Context(), false);
    }

    private static void runRoundTrip(Context registryCtx, Context serverCtx,
                                     Context clientCtx, boolean sameContext) {
        TestSupport.assumeNative();

        String suffix = Long.toUnsignedString(System.nanoTime(), 36);
        String regPub = TestSupport.tcpEndpoint();
        String regRouter = TestSupport.tcpEndpoint();
        String serverEndpoint = TestSupport.tcpEndpoint();
        String clientEndpoint = TestSupport.tcpEndpoint();

        try (registryCtx; serverCtx; clientCtx;
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

            clientDiscovery.connectRegistry(regRouter);
            serverDiscovery.connectRegistry(regRouter);
            clientDiscovery.subscribe("perf-server");
            serverDiscovery.subscribe("c0");

            serverReceiver.bind(serverEndpoint);
            serverReceiver.connectRegistry(regRouter);
            serverReceiver.register("perf-server", serverReceiver.lastEndpoint(), 1);
            clientReceiver.bind(clientEndpoint);
            clientReceiver.connectRegistry(regRouter);
            clientReceiver.register("c0", clientReceiver.lastEndpoint(), 1);

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
              ("hello-" + suffix).getBytes(StandardCharsets.UTF_8))) {
                clientGateway.sendTo("perf-server", outbound, SendFlag.DONTWAIT);
            }

            TestSupport.waitUntil(() -> serverPoller.pollCount(50) > 0,
              TestSupport.DEFAULT_TIMEOUT_MS,
              "server receiver poller never became readable");
            assertEquals(SERVER_TAG, serverPoller.readyTag(0));
            String serverPayload;
            try (Receiver.ReceiverMessages received = TestSupport.receiverRecvWithTimeout(
              serverReceiver, TestSupport.DEFAULT_TIMEOUT_MS)) {
                Message[] parts = received.parts();
                serverPayload = new String(parts[parts.length - 1].data(),
                  StandardCharsets.UTF_8);
            }
            assertEquals("hello-" + suffix, serverPayload);

            try (Message echo = Message.fromBytes(serverPayload.getBytes(
              StandardCharsets.UTF_8))) {
                serverGateway.sendTo("c0", echo, SendFlag.DONTWAIT);
            }

            TestSupport.waitUntil(() -> clientPoller.pollCount(50) > 0,
              TestSupport.DEFAULT_TIMEOUT_MS,
              "client receiver poller never became readable");
            assertEquals(CLIENT_TAG, clientPoller.readyTag(0));
            try (Receiver.ReceiverMessages received = TestSupport.receiverRecvWithTimeout(
              clientReceiver, TestSupport.DEFAULT_TIMEOUT_MS)) {
                Message[] parts = received.parts();
                String clientPayload = new String(parts[parts.length - 1].data(),
                  StandardCharsets.UTF_8);
                assertEquals("hello-" + suffix, clientPayload);
            }
        }
    }
}
