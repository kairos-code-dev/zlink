package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
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

public class TestGatewayHandoverPortedTest {
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

            discovery.connectRegistry(regRouter);
            discovery.subscribe(service);

            try (Receiver provider1 = new Receiver(ctx)) {
                provider1.setRoutingId(providerRoutingId);
                provider1.bind(TestSupport.tcpEndpoint());
                provider1.connectRegistry(regRouter);
                provider1.register(service, provider1.lastEndpoint(), 1);
                waitRegisterOk(provider1, service);
                waitGatewayReady(gateway, service);
                sendGatewayMessage(gateway, service, "msg1");
                assertEquals("msg1", recvProviderPayload(provider1));
                provider1.unregister(service);
            }

            TestSupport.sleepMs(300);

            try (Receiver provider2 = new Receiver(ctx)) {
                provider2.setRoutingId(providerRoutingId);
                provider2.bind(TestSupport.tcpEndpoint());
                provider2.connectRegistry(regRouter);
                provider2.register(service, provider2.lastEndpoint(), 1);
                waitRegisterOk(provider2, service);
                waitGatewayReady(gateway, service);
                sendGatewayMessage(gateway, service, "msg2");
                assertEquals("msg2", recvProviderPayload(provider2));
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

            provider.setRoutingId(providerRoutingId);
            provider.bind(TestSupport.tcpEndpoint());
            provider.connectRegistry(regRouter);
            provider.register(service, provider.lastEndpoint(), 1);
            waitRegisterOk(provider, service);

            try (Discovery discovery1 = new Discovery(ctx, ServiceType.GATEWAY);
                 Gateway gateway1 = new Gateway(ctx, discovery1, gwRoutingId)) {
                discovery1.connectRegistry(regRouter);
                discovery1.subscribe(service);
                waitGatewayReady(gateway1, service);
                sendGatewayMessage(gateway1, service, "gw-1");
                assertEquals("gw-1", recvProviderPayload(provider));
            }

            TestSupport.sleepMs(300);

            try (Discovery discovery2 = new Discovery(ctx, ServiceType.GATEWAY);
                 Gateway gateway2 = new Gateway(ctx, discovery2, gwRoutingId)) {
                discovery2.connectRegistry(regRouter);
                discovery2.subscribe(service);
                waitGatewayReady(gateway2, service);
                sendGatewayMessage(gateway2, service, "gw-2");
                assertEquals("gw-2", recvProviderPayload(provider));
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
        try (Message msg = Message.fromBytes(payload.getBytes(StandardCharsets.UTF_8))) {
            gateway.sendTo(service, msg, SendFlag.DONTWAIT);
        }
    }

    private static String recvProviderPayload(Receiver receiver) {
        try (Receiver.ReceiverMessages received = TestSupport.receiverRecvWithTimeout(
          receiver, TestSupport.DEFAULT_TIMEOUT_MS)) {
            Message[] parts = received.parts();
            return parts.length == 0
              ? ""
              : new String(parts[parts.length - 1].data(), StandardCharsets.UTF_8);
        }
    }

    private static String uniqueSuffix() {
        return Long.toUnsignedString(System.nanoTime(), 36);
    }
}
