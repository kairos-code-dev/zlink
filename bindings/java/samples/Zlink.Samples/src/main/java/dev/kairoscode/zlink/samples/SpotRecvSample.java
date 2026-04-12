package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RecvException;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RecvResult;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.time.Instant;

public final class SpotRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String published = SampleSupport.SPOT_TOPIC + "/" + SampleSupport.SPOT_PAYLOAD;

        try (Context ctx = new Context();
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = new Spot(publisherNode);
             Spot subscriber = new Spot(subscriberNode)) {
            publisherNode.bind("tcp://127.0.0.1:0");
            String endpoint = publisherNode.statusSnapshot().localEndpoint();
            subscriberNode.connectPeer(endpoint);
            subscriber.setSubscription(SampleSupport.SPOT_TOPIC);
            SampleSupport.waitSpotPeerConnected(subscriberNode);

            Instant deadline = Instant.now().plus(Duration.ofSeconds(5));
            while (Instant.now().isBefore(deadline)) {
                try (Message payload = Message.copyOfUtf8(SampleSupport.SPOT_PAYLOAD)) {
                    publisher.publish(SampleSupport.SPOT_TOPIC, payload);
                }
                try (var topicMessage = subscriber.subscribe(RecvFlags.DONT_WAIT)) {
                    String value = topicMessage.topicId() + "/"
                        + topicMessage.singlePartOrThrow().toUtf8String();
                    if (!published.equals(value)) {
                        throw new IllegalStateException(
                            "unexpected delivery: " + value);
                    }
                    System.out.println("[spot/recv] publish: \"" + published
                        + "\" \u2192 subscribe: \"" + value + "\"");
                    return;
                } catch (RecvException ex) {
                    if (ex.getResult() != RecvResult.NO_DATA) {
                        throw ex;
                    }
                }
                Thread.onSpinWait();
            }
            throw new IllegalStateException("spot delivery did not arrive");
        }
    }
}
