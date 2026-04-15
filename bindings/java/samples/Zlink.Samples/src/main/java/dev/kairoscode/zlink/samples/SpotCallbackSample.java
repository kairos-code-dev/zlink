package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.ServiceType;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.time.Instant;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public final class SpotCallbackSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String serviceName = "spot-callback-service";
        String endpoint = SampleSupport.tcpEndpoint();
        String registryPub = SampleSupport.tcpEndpoint();
        String registryRouter = SampleSupport.tcpEndpoint();
        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<dev.kairoscode.zlink.SpotDispatchEvent> eventRef =
            new AtomicReference<>();
        AtomicReference<Throwable> callbackError = new AtomicReference<>();

        try (Context ctx = new Context();
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, ServiceType.SPOT,
                 serviceName);
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            publisherNode.attachDiscovery(discovery);
            subscriberNode.attachDiscovery(discovery);
            publisherNode.bind(endpoint);
            subscriberNode.connectPeer(endpoint);
            subscriber.setSubscription(SampleSupport.SPOT_TOPIC);
            SampleSupport.waitSpotPeerConnected(publisherNode);
            SampleSupport.waitSpotPeerConnected(subscriberNode);

            subscriber.onDispatchEvent(event -> {
                if (event != dev.kairoscode.zlink.SpotDispatchEvent.SUBSCRIBE_READABLE) {
                    return;
                }
                eventRef.set(event);
                delivered.countDown();
            });

            try (Message payload = Message.copyOfUtf8(SampleSupport.SPOT_PAYLOAD)) {
                publisher.publish(serviceName, SampleSupport.SPOT_TOPIC,
                    payload);
            }

            SampleSupport.await(delivered, "spot callback");
            if (callbackError.get() != null) {
                throw new IllegalStateException("spot callback failed",
                    callbackError.get());
            }
            if (eventRef.get() != dev.kairoscode.zlink.SpotDispatchEvent.SUBSCRIBE_READABLE) {
                throw new IllegalStateException("unexpected dispatch event");
            }
            System.out.println("[spot/callback] event: \""
                + eventRef.get() + "\"");
        }
    }
}
