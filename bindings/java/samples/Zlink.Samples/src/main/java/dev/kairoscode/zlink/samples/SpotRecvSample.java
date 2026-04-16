package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

public final class SpotRecvSample {
    public static void main(String[] args) throws InterruptedException {
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

            CountDownLatch delivered = new CountDownLatch(1);
            AtomicReference<String> receivedService = new AtomicReference<>();
            AtomicReference<String> receivedValue = new AtomicReference<>();
            AtomicReference<Throwable> callbackError = new AtomicReference<>();

            spot.onDispatchEvent(event -> {
                if (event != SpotDispatchEvent.SUBSCRIBE_READABLE) {
                    return;
                }
                try (var topicMessage = spot.subscribe(RecvFlags.DONT_WAIT)) {
                    receivedService.set(topicMessage.serviceName()
                        .orElse(""));
                    receivedValue.set(topicMessage.topic() + "/"
                        + topicMessage.singlePartOrThrow().toUtf8String());
                } catch (Throwable ex) {
                    callbackError.set(ex);
                } finally {
                    delivered.countDown();
                }
            });
            spot.setSubscription(topic);

            try (Message payload = Message.copyOfUtf8(SampleSupport.SPOT_PAYLOAD)) {
                spot.publish(serviceName, topic, payload);
            }

            if (!delivered.await(5, TimeUnit.SECONDS)) {
                throw new IllegalStateException("spot dispatch callback did not deliver");
            }
            if (callbackError.get() != null) {
                throw new IllegalStateException("spot dispatch callback failed",
                    callbackError.get());
            }
            if (!serviceName.equals(receivedService.get())
                || !published.equals(receivedValue.get())) {
                throw new IllegalStateException(
                    "unexpected delivery: " + receivedService.get()
                        + "/" + receivedValue.get());
            }

            System.out.println("[spot/recv] service: \"" + serviceName
                + "\" tick: 1 publish: \"" + published
                + "\" -> recv: \"" + receivedValue.get() + "\"");
        }
    }
}
