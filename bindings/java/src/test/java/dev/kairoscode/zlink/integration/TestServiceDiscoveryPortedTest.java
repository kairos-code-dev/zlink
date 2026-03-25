package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.receiver.ReceiverInfo;
import dev.kairoscode.zlink.service.registry.Registry;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestServiceDiscoveryPortedTest {
    @Test
    public void testDiscoveryProviderRegistrationAndRemoval() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            String regPub = TestSupport.inprocEndpoint("reg-pub-basic");
            String regRouter = TestSupport.inprocEndpoint("reg-router-basic");

            try (Registry registry = new Registry(ctx);
                 Discovery discovery = new Discovery(ctx, ServiceType.SPOT);
                 Receiver receiver = new Receiver(ctx)) {
                registry.setEndpoints(regPub, regRouter);
                registry.start();

                discovery.connectRegistry(regRouter);

                receiver.bind(TestSupport.tcpEndpoint());
                String advertise = receiver.lastEndpoint();
                receiver.connectRegistry(regRouter);
                receiver.register("test-svc", advertise, 1);

                TestSupport.waitUntil(
                  () -> discovery.receiverCount("test-svc") > 0,
                  TestSupport.DEFAULT_TIMEOUT_MS,
                  "provider did not appear in discovery");

                assertEquals(1, discovery.receiverCount("test-svc"));
                assertTrue(discovery.serviceAvailable("test-svc"));

                List<ReceiverInfo> infos = discovery.getReceivers("test-svc");
                assertEquals(1, infos.size());
                assertEquals("test-svc", infos.get(0).serviceName());
                assertEquals(advertise, infos.get(0).endpoint());
                assertEquals(1, infos.get(0).weight());
                assertTrue(infos.get(0).routingId().length > 0);

                receiver.unregister("test-svc");
                TestSupport.waitUntil(
                  () -> discovery.receiverCount("test-svc") == 0,
                  TestSupport.DEFAULT_TIMEOUT_MS,
                  "provider did not disappear from discovery");
            }
        }
    }

    @Test
    public void testDiscoverySeesMultipleServices() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            String regPub = TestSupport.inprocEndpoint("reg-pub-filter");
            String regRouter = TestSupport.inprocEndpoint("reg-router-filter");

            try (Registry registry = new Registry(ctx);
                 Discovery discovery = new Discovery(ctx, ServiceType.SPOT);
                 Receiver receiverA = new Receiver(ctx);
                 Receiver receiverB = new Receiver(ctx)) {
                registry.setEndpoints(regPub, regRouter);
                registry.start();

                discovery.connectRegistry(regRouter);

                receiverA.bind(TestSupport.tcpEndpoint());
                receiverB.bind(TestSupport.tcpEndpoint());
                String advertiseA = receiverA.lastEndpoint();
                String advertiseB = receiverB.lastEndpoint();

                receiverA.connectRegistry(regRouter);
                receiverB.connectRegistry(regRouter);
                receiverA.register("svc-A", advertiseA, 10);
                receiverB.register("svc-B", advertiseB, 20);

                TestSupport.waitUntil(
                  () -> discovery.receiverCount("svc-A") > 0,
                  TestSupport.DEFAULT_TIMEOUT_MS,
                  "svc-A provider not discovered");

                List<ReceiverInfo> aInfos = discovery.getReceivers("svc-A");
                assertEquals(1, aInfos.size());
                assertEquals(advertiseA, aInfos.get(0).endpoint());
                assertEquals(10, aInfos.get(0).weight());

                TestSupport.waitUntil(
                  () -> discovery.receiverCount("svc-B") > 0,
                  TestSupport.DEFAULT_TIMEOUT_MS,
                  "svc-B provider not discovered");

                List<ReceiverInfo> bInfos = discovery.getReceivers("svc-B");
                assertEquals(1, bInfos.size());
                assertEquals(advertiseB, bInfos.get(0).endpoint());
                assertEquals(20, bInfos.get(0).weight());
            }
        }
    }
}
