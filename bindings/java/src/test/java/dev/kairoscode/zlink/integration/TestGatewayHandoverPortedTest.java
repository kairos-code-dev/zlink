package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Discovery;
import dev.kairoscode.zlink.Gateway;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Receiver;
import dev.kairoscode.zlink.Registry;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.ZlinkException;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.fail;

public class TestGatewayHandoverPortedTest {
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EHOSTUNREACH = 113;

    @Test
    public void testGatewayHandoverAfterProviderRestartWithSameRoutingId() {
        TestSupport.assumeNative();

        String suffix = uniqueSuffix();
        String service = "ho-svc-" + suffix;
        String providerRoutingId = "PROV-HO-" + suffix;
        try (Context ctx = new Context();
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, ServiceType.GATEWAY);
             Gateway gateway = new Gateway(ctx, discovery)) {
            String regPub = TestSupport.inprocEndpoint("reg-pub-ho1-" + suffix);
            String regRouter = TestSupport.inprocEndpoint("reg-router-ho1-" + suffix);
            registry.setEndpoints(regPub, regRouter);
            registry.start();
            TestSupport.sleepMs(100);

            discovery.connectRegistry(regPub);
            discovery.subscribe(service);

            try (Receiver provider1 = new Receiver(ctx)) {
                provider1.bind(TestSupport.tcpEndpoint());
                try (Socket router1 = provider1.routerSocket()) {
                    router1.setSockOpt(SocketOption.PROBE_ROUTER, 1);
                    router1.setSockOpt(SocketOption.ROUTING_ID,
                        providerRoutingId.getBytes(StandardCharsets.UTF_8));
                    String advertise = TestSupport.bytesToString(
                        router1.getSockOptBytes(SocketOption.LAST_ENDPOINT, 256));

                    provider1.connectRegistry(regRouter);
                    provider1.register(service, advertise, 1);
                    waitRegisterOk(provider1, service);
                    TestSupport.sleepMs(200);

                    waitGatewayReady(gateway, service);
                    TestSupport.sleepMs(200);
                    sendGatewayMessage(gateway, service, "msg1");
                    assertEquals("msg1", recvProviderPayload(router1));
                }
                provider1.unregister(service);
            }

            TestSupport.sleepMs(300);

            try (Receiver provider2 = new Receiver(ctx)) {
                provider2.bind(TestSupport.tcpEndpoint());
                try (Socket router2 = provider2.routerSocket()) {
                    router2.setSockOpt(SocketOption.PROBE_ROUTER, 1);
                    router2.setSockOpt(SocketOption.ROUTING_ID,
                        providerRoutingId.getBytes(StandardCharsets.UTF_8));
                    String advertise = TestSupport.bytesToString(
                        router2.getSockOptBytes(SocketOption.LAST_ENDPOINT, 256));

                    provider2.connectRegistry(regRouter);
                    provider2.register(service, advertise, 1);
                    waitRegisterOk(provider2, service);
                    TestSupport.sleepMs(300);

                    waitGatewayReady(gateway, service);
                    TestSupport.sleepMs(200);
                    sendGatewayMessage(gateway, service, "msg2");
                    assertEquals("msg2", recvProviderPayload(router2));
                }
            }
        }
    }

    @Test
    public void testProviderHandoverAfterGatewayReconnectWithSameRoutingId() {
        TestSupport.assumeNative();

        String suffix = uniqueSuffix();
        String service = "ho-svc2-" + suffix;
        String gwRoutingId = "GW-HO-" + suffix;
        String providerRoutingId = "PROV-HO2-" + suffix;
        try (Context ctx = new Context();
             Registry registry = new Registry(ctx);
             Receiver provider = new Receiver(ctx)) {
            String regPub = TestSupport.inprocEndpoint("reg-pub-ho2-" + suffix);
            String regRouter = TestSupport.inprocEndpoint("reg-router-ho2-" + suffix);
            registry.setEndpoints(regPub, regRouter);
            registry.start();

            provider.bind(TestSupport.tcpEndpoint());
                try (Socket providerRouter = provider.routerSocket()) {
                    providerRouter.setSockOpt(SocketOption.PROBE_ROUTER, 1);
                    providerRouter.setSockOpt(SocketOption.ROUTING_ID,
                        providerRoutingId.getBytes(StandardCharsets.UTF_8));
                    providerRouter.setSockOpt(SocketOption.ROUTER_HANDOVER, 1);

                String advertise = TestSupport.bytesToString(
                    providerRouter.getSockOptBytes(SocketOption.LAST_ENDPOINT,
                        256));

                provider.connectRegistry(regRouter);
                provider.register(service, advertise, 1);
                waitRegisterOk(provider, service);

                try (Discovery discovery1 = new Discovery(ctx, ServiceType.GATEWAY);
                     Gateway gateway1 = new Gateway(ctx, discovery1, gwRoutingId)) {
                    discovery1.connectRegistry(regPub);
                    discovery1.subscribe(service);

                    waitGatewayReady(gateway1, service);
                    sendGatewayMessage(gateway1, service, "gw-1");
                    assertEquals("gw-1", recvProviderPayload(providerRouter));
                }

                TestSupport.sleepMs(400);

                try (Discovery discovery2 = new Discovery(ctx, ServiceType.GATEWAY);
                     Gateway gateway2 = new Gateway(ctx, discovery2, gwRoutingId)) {
                    discovery2.connectRegistry(regPub);
                    discovery2.subscribe(service);

                    waitGatewayReady(gateway2, service);
                    sendGatewayMessage(gateway2, service, "gw-2");
                    assertEquals("gw-2", recvProviderPayload(providerRouter));
                }
            }
        }
    }

    private static void waitRegisterOk(Receiver receiver, String service) {
        TestSupport.waitUntil(() -> receiver.registerResult(service).status() == 0,
            TestSupport.DEFAULT_TIMEOUT_MS,
            "provider register did not succeed for service=" + service);
    }

    private static void waitGatewayReady(Gateway gateway, String service) {
        TestSupport.waitUntil(() -> gateway.connectionCount(service) > 0,
            TestSupport.DEFAULT_TIMEOUT_MS,
            "gateway did not connect service=" + service);
    }

    private static void sendGatewayMessage(Gateway gateway, String service,
                                           String payload) {
        byte[] bytes = payload.getBytes(StandardCharsets.UTF_8);
        long deadline = System.currentTimeMillis() + TestSupport.DEFAULT_TIMEOUT_MS;
        ZlinkException last = null;
        while (System.currentTimeMillis() < deadline) {
            try (Message msg = Message.fromBytes(bytes)) {
                gateway.send(service, msg, SendFlag.DONTWAIT);
                return;
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (errno != ERRNO_EAGAIN && errno != ERRNO_EHOSTUNREACH)
                    throw ex;
                last = ex;
                TestSupport.sleepMs(2);
            }
        }
        if (last != null)
            throw last;
        fail("gateway send timeout");
    }

    private static String recvProviderPayload(Socket providerRouter) {
        TestSupport.recvWithTimeout(providerRouter, 256,
            TestSupport.DEFAULT_TIMEOUT_MS);
        byte[] payload = new byte[0];
        for (int i = 0; i < 6; i++) {
            payload = TestSupport.recvWithTimeout(providerRouter, 512,
                TestSupport.DEFAULT_TIMEOUT_MS);
            if (payload.length > 0)
                break;
        }
        return new String(payload, StandardCharsets.UTF_8);
    }

    private static String uniqueSuffix() {
        return Long.toUnsignedString(System.nanoTime(), 36);
    }
}
