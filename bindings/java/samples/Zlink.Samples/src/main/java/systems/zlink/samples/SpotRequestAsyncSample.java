package systems.zlink.samples;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.service.spot.SpotNode;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class SpotRequestAsyncSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        String channelName = "orders";

        try (Context ctx = Zlink.createContext();
             SpotNode requesterNode = ctx.createSpotNode();
             DealerSocket requesterDealer = ctx.createDealerSocket();
             RouterSocket requesterRouter = ctx.createRouterSocket();
             var requester = requesterNode.createSpot()) {
            requesterRouter.bind(endpoint);
            requesterDealer.connect(endpoint);
            requesterNode.attachChannelDealerManual(channelName, requesterDealer);

            Thread responder = new Thread(() -> {
                try (systems.zlink.contracts.messaging.Received received = new systems.zlink.contracts.messaging.Received()) {
                    requesterRouter.recv(received, systems.zlink.contracts.sockets.RecvFlags.NONE);
                    if (!"spot-ping".equals(SampleSupport.singleUtf8(received))) {
                        throw new IllegalStateException("unexpected spot request");
                    }
                    try (Message reply = Message.from("spot-pong")) {
                        received.reply().message(reply).submit();
                    }
                }
            }, "spot-request-async-responder");
            responder.start();

            CountDownLatch replyLatch = new CountDownLatch(1);
            final List<Message>[] replyHolder = new List[] { List.of() };
            final RequestResult[] resultHolder = new RequestResult[] { RequestResult.TIMED_OUT };
            try (Message request = Message.from("spot-ping")) {
                requester.requestToChannel(channelName)
                    .message(request)
                    .timeout(Duration.ofSeconds(5))
                    .submit((requestResult, replyParts) -> {
                        resultHolder[0] = requestResult;
                        replyHolder[0] = replyParts;
                        replyLatch.countDown();
                    });
            }
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
