package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RequestResult;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class SpotRequestAsyncSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        String serviceName = "sample";

        try (Context ctx = new Context();
             SpotNode requesterNode = new SpotNode(ctx);
             SpotNode responderNode = new SpotNode(ctx);
             Spot requester = requesterNode.createSpot();
             Spot responderSpot = responderNode.createSpot()) {
            requesterNode.setRoutingId(RoutingId.fromBytes("spot-requester-node".getBytes()));
            responderNode.setRoutingId(RoutingId.fromBytes("spot-responder-node".getBytes()));
            requester.setRoutingId(RoutingId.fromBytes("spot-requester".getBytes()));
            responderSpot.setRoutingId(RoutingId.fromBytes("spot-responder".getBytes()));
            responderNode.bind(endpoint);
            requesterNode.connectPeer(endpoint);
            SampleSupport.waitSpotPeerConnected(requesterNode);

            Thread responder = new Thread(() -> {
                try (var received = responderSpot.recvRouted(RecvFlags.NONE)) {
                    if (!"spot-ping".equals(SampleSupport.singleUtf8(received))) {
                        throw new IllegalStateException("unexpected spot request");
                    }
                    try (Message reply = Message.copyOfUtf8("spot-pong")) {
                        received.reply(reply);
                    }
                }
            }, "spot-request-async-responder");
            responder.start();

            CountDownLatch replyLatch = new CountDownLatch(1);
            final List<Message>[] replyHolder = new List[] { List.of() };
            final RequestResult[] resultHolder = new RequestResult[] { RequestResult.TIMED_OUT };
            requester.requestToSpot(
                responderNode.routingId(),
                responderSpot.routingId(),
                List.of(Message.copyOfUtf8("spot-ping")),
                (requestResult, replyParts) -> {
                    resultHolder[0] = requestResult;
                    replyHolder[0] = replyParts;
                    replyLatch.countDown();
                },
                SendFlags.NONE,
                Duration.ofSeconds(5));
            if (!replyLatch.await(5, TimeUnit.SECONDS)) {
                throw new IllegalStateException("spot request async callback timed out");
            }
            if (resultHolder[0] != RequestResult.OK) {
                throw new IllegalStateException("unexpected request result " + resultHolder[0]);
            }
            var reply = replyHolder[0];
            try {
                if (reply.size() != 1 || !"spot-pong".equals(reply.get(0).toUtf8String())) {
                    throw new IllegalStateException("unexpected spot reply");
                }
            } finally {
                reply.forEach(Message::close);
            }

            responder.join(TimeUnit.SECONDS.toMillis(5));
            System.out.println("[spot/request/async] request: \"spot-ping\" -> reply: \"spot-pong\"");
        }
    }
}
