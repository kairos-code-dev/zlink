package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.ServiceType;
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
        String serviceName = "sample";
        String topic = SampleSupport.SPOT_TOPIC;
        String published = topic + "/" + SampleSupport.SPOT_PAYLOAD;
        String registryPub = SampleSupport.tcpEndpoint();
        String registryRouter = SampleSupport.tcpEndpoint();
        String publisherEndpoint = SampleSupport.tcpEndpoint();
        String subscriberEndpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, ServiceType.SPOT, serviceName);
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            publisherNode.attachDiscovery(discovery);
            subscriberNode.attachDiscovery(discovery);
            publisherNode.bind(publisherEndpoint);
            subscriberNode.bind(subscriberEndpoint);
            subscriber.setSubscription(topic);

            Instant deadline = Instant.now().plus(Duration.ofSeconds(5));
            while (Instant.now().isBefore(deadline)) {
                try (Message payload = Message.copyOfUtf8(SampleSupport.SPOT_PAYLOAD)) {
                    publisher.publish(serviceName, topic, payload);
                }
                try (var topicMessage = subscriber.subscribe(RecvFlags.DONT_WAIT)) {
                    if (!java.util.Optional.of(serviceName)
                        .equals(topicMessage.serviceName())) {
                        throw new IllegalStateException(
                            "unexpected service: " + topicMessage.serviceName());
                    }
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
                        && ex.getResult() != RecvResult.BUSY
                        && ex.getInternalErrno() != 2) {
                        throw ex;
                    }
                }
                Thread.onSpinWait();
            }
            throw new IllegalStateException("spot delivery did not arrive");
        }
    }
}
