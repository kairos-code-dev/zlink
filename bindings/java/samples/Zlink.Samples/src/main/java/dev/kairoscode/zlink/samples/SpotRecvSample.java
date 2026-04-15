package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RecvException;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RecvResult;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.ServiceType;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.time.Instant;

public final class SpotRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String serviceName = "spot-recv-service";
        String topic = SampleSupport.SPOT_TOPIC;
        String published = topic + "/" + SampleSupport.SPOT_PAYLOAD;
        String endpoint = SampleSupport.tcpEndpoint();
        String registryPub = SampleSupport.tcpEndpoint();
        String registryRouter = SampleSupport.tcpEndpoint();

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
            subscriber.setSubscription(topic);
            SampleSupport.waitSpotPeerConnected(publisherNode);
            SampleSupport.waitSpotPeerConnected(subscriberNode);
            sleepQuietly(250L);

            Instant deadline = Instant.now().plus(Duration.ofSeconds(5));
            while (Instant.now().isBefore(deadline)) {
                try (Message payload = Message.copyOfUtf8(SampleSupport.SPOT_PAYLOAD)) {
                    publisher.publish(serviceName, topic, payload);
                }
                sleepQuietly(10L);
                try (var topicMessage = subscriber.subscribe(RecvFlags.DONT_WAIT)) {
                    String value = topicMessage.topic() + "/"
                        + topicMessage.singlePartOrThrow().toUtf8String();
                    if (!published.equals(value)) {
                        throw new IllegalStateException(
                            "unexpected delivery: " + value);
                    }
                    System.out.println("[spot/recv] publish: \"" + published
                        + "\" \u2192 subscribe: \"" + value + "\"");
                    return;
                } catch (RecvException ex) {
                    if (ex.getResult() != RecvResult.NO_DATA
                        && ex.getResult() != RecvResult.BUSY) {
                        throw ex;
                    }
                }
                Thread.onSpinWait();
            }
            throw new IllegalStateException("spot delivery did not arrive");
        }
    }

    private static void sleepQuietly(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot settle interrupted", ex);
        }
    }
}
