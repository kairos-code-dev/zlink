package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.RecvException;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RecvResult;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.time.Instant;

public final class SpotRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String serviceName = "sample";
        String topic = SampleSupport.SPOT_TOPIC;
        String published = topic + "/" + SampleSupport.SPOT_PAYLOAD;
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             PubSocket pubSocket = new PubSocket(ctx);
             SubSocket subSocket = new SubSocket(ctx);
             SpotNode node = new SpotNode(ctx);
             Spot spot = node.createSpot()) {
            pubSocket.bind(endpoint);
            subSocket.connect(endpoint);
            node.attachPubSub(serviceName, pubSocket, subSocket);
            spot.setSubscription(topic);

            Instant deadline = Instant.now().plus(Duration.ofSeconds(5));
            while (Instant.now().isBefore(deadline)) {
                try (Message payload = Message.copyOfUtf8(SampleSupport.SPOT_PAYLOAD)) {
                    spot.publish(serviceName, topic, payload);
                }
                try (var topicMessage = spot.subscribe(RecvFlags.DONT_WAIT)) {
                    String value = topicMessage.topic() + "/"
                        + topicMessage.singlePartOrThrow().toUtf8String();
                    if (!published.equals(value)) {
                        throw new IllegalStateException(
                            "unexpected delivery: " + value);
                    }
                    System.out.println("[spot/recv] service: \"" + serviceName
                        + "\" tick: 1 publish: \"" + published
                        + "\" -> recv: \"" + value + "\"");
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
}
