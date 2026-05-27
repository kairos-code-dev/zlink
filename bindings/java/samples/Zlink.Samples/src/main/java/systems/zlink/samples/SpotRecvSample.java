package systems.zlink.samples;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.RecvFlags;
import systems.zlink.contracts.TopicMessage;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import java.time.Duration;
import java.time.Instant;

public final class SpotRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String channelName = "sample";
        String topic = SampleSupport.SPOT_TOPIC;
        String published = topic + "/" + SampleSupport.SPOT_PAYLOAD;
        String publisherEndpoint = SampleSupport.tcpEndpoint();
        String subscriberEndpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            publisherNode.setRoutingId(RoutingId.from(
                "z-java-spot-recv-publisher".getBytes(java.nio.charset.StandardCharsets.UTF_8)));
            subscriberNode.setRoutingId(RoutingId.from(
                "a-java-spot-recv-subscriber".getBytes(java.nio.charset.StandardCharsets.UTF_8)));
            publisher.setRoutingId(RoutingId.from(
                "z-java-spot-recv-publisher-spot".getBytes(java.nio.charset.StandardCharsets.UTF_8)));
            subscriber.setRoutingId(RoutingId.from(
                "a-java-spot-recv-subscriber-spot".getBytes(java.nio.charset.StandardCharsets.UTF_8)));
            publisherNode.setPubBind(publisherEndpoint);
            subscriberNode.setPubBind(subscriberEndpoint);
            publisherNode.connectPeer(subscriberEndpoint);
            subscriberNode.connectPeer(publisherEndpoint);
            subscriber.setSubscription(topic);
            SampleSupport.waitSpotPeerConnected(publisherNode);
            SampleSupport.waitSpotPeerConnected(subscriberNode);

            Instant deadline = Instant.now().plus(Duration.ofSeconds(5));
            while (Instant.now().isBefore(deadline)) {
                try (Message payload = Message.from(SampleSupport.SPOT_PAYLOAD)) {
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
