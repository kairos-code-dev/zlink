package systems.zlink.samples;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.RoutingId;
import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.registry.Registry;
import systems.zlink.service.registry.AutoConnectType;
import systems.zlink.RecvFlags;
import systems.zlink.TopicMessage;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.time.Instant;

public final class SpotRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String channelName = "sample";
        String topic = SampleSupport.SPOT_TOPIC;
        String published = topic + "/" + SampleSupport.SPOT_PAYLOAD;
        String registryPub = SampleSupport.tcpEndpoint();
        String registryRouter = SampleSupport.tcpEndpoint();
        String publisherEndpoint = SampleSupport.tcpEndpoint();
        String subscriberEndpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             Registry registry = new Registry(ctx);
             Discovery publisherDiscovery = new Discovery(ctx,
                 AutoConnectType.SPOT_MESH, channelName);
             Discovery subscriberDiscovery = new Discovery(ctx,
                 AutoConnectType.SPOT_MESH, channelName);
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            publisherNode.setRoutingId(RoutingId.fromBytes(
                "z-java-spot-recv-publisher".getBytes(java.nio.charset.StandardCharsets.UTF_8)));
            subscriberNode.setRoutingId(RoutingId.fromBytes(
                "a-java-spot-recv-subscriber".getBytes(java.nio.charset.StandardCharsets.UTF_8)));
            publisher.setRoutingId(RoutingId.fromBytes(
                "z-java-spot-recv-publisher-spot".getBytes(java.nio.charset.StandardCharsets.UTF_8)));
            subscriber.setRoutingId(RoutingId.fromBytes(
                "a-java-spot-recv-subscriber-spot".getBytes(java.nio.charset.StandardCharsets.UTF_8)));
            registry.bind(registryPub, registryRouter);
            registry.setBroadcastInterval(Duration.ofMillis(50));
            publisherDiscovery.connectRegistry(registryRouter);
            subscriberDiscovery.connectRegistry(registryRouter);
            publisherNode.bind(publisherEndpoint);
            subscriberNode.bind(subscriberEndpoint);
            publisherNode.attachDiscovery(publisherDiscovery);
            subscriberNode.attachDiscovery(subscriberDiscovery);
            subscriber.setSubscription(topic);
            SampleSupport.waitSpotPeerConnected(publisherNode);
            SampleSupport.waitSpotPeerConnected(subscriberNode);

            Instant deadline = Instant.now().plus(Duration.ofSeconds(5));
            while (Instant.now().isBefore(deadline)) {
                try (Message payload = Message.copyOfUtf8(SampleSupport.SPOT_PAYLOAD)) {
                    publisher.publish(topic).message(payload).submit();
                }
                try (TopicMessage topicMessage = new TopicMessage()) {
                    if (!subscriber.subscribe(topicMessage, RecvFlags.DONT_WAIT)) {
                        Thread.onSpinWait();
                        continue;
                    }
                    String value = topicMessage.topic() + "/"
                        + topicMessage.singlePartOrThrow().toUtf8String();
                    if (!published.equals(value)) {
                        throw new IllegalStateException(
                            "unexpected delivery: " + value);
                    }
                    System.out.println("[spot/recv] service: \"" + channelName
                        + "\" tick: 1 publish: \"" + published
                        + "\" -> recv: \"" + value + "\"");
                    return;
                }
            }
            throw new IllegalStateException("spot delivery did not arrive");
        }
    }
}
