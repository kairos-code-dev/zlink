package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.TopicMessage;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

public class SocketContractTest {
    @Test
    public void sendAndRecvUseCanonicalMultipartSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.PAIR);
             Socket client = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.inprocEndpoint("socket-contract");
            server.bind(endpoint);
            client.connect(endpoint);

            try (Message outbound = Message.copyOfUtf8("pair-contract")) {
                client.send(outbound);
            }

            try (var received = server.recv()) {
                assertArrayEquals("pair-contract".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
            }
        }
    }

    @Test
    public void publishAndSubscribeUseCanonicalTopicAwareSurface() {
        TestSupport.assumeNative();

        int readyEvents = dev.kairoscode.zlink.MonitorEventType.SUB_DELIVERY_READY_CHANGED
            .getValue()
            | dev.kairoscode.zlink.MonitorEventType.PUB_DELIVERY_READY_CHANGED
                .getValue();
        try (Context ctx = new Context();
             Socket pub = new Socket(ctx, SocketType.PUB);
             Socket sub = new Socket(ctx, SocketType.SUB);
             var pubMonitor = pub.monitorOpen(readyEvents);
             var subMonitor = sub.monitorOpen(readyEvents)) {
            String endpoint = TestSupport.inprocEndpoint("socket-pubsub-contract");
            pub.bind(endpoint);
            sub.setSubscription("socket-topic");
            sub.connect(endpoint);
            subMonitor.recv();
            pubMonitor.recv();

            try (Message payload = Message.copyOfUtf8("socket-payload")) {
                pub.publish("socket-topic", payload);
            }

            try (TopicMessage received = sub.subscribe()) {
                assertEquals("socket-topic", received.topicId());
                assertArrayEquals("socket-payload".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
            }
        }
    }
}
