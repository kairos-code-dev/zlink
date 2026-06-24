package systems.zlink.samples;

import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorJoinRequest;
import systems.zlink.contracts.service.spot.ActorReceived;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotDispatchEvent;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.sockets.StreamSocket;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

public final class ActorSinglePlayerQueueSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot spot = node.createSpot();
             StreamSocket stream = ctx.createStreamSocket();
             var monitor = stream.monitorOpen(
                 systems.zlink.contracts.eventing.MonitorEventType.ACCEPTED)) {
            Actor actor = node.createActor("single-player");
            ActorRef actorRef = actor.ref();
            List<String> payloads = new ArrayList<>();
            List<RequestResult> replies = new ArrayList<>();

            spot.setDispatchHandler(info -> {
                if (info.event() == SpotDispatchEvent.ACTOR_JOIN_READABLE) {
                    try (ActorJoinRequest request =
                             spot.recvActorJoin(RecvFlags.DONT_WAIT)) {
                        if (request != null) {
                            try (Message reply = Message.from("accepted")) {
                                spot.replyActorJoin(request, 0)
                                  .message(reply)
                                  .submit();
                            }
                        }
                    }
                    return;
                }
                if (info.event() == SpotDispatchEvent.ACTOR_READABLE) {
                    for (ActorReceived part : info.actorMessages()) {
                        try (part) {
                            payloads.add(part.message().toUtf8String());
                        }
                    }
                }
            });

            stream.bind(endpoint);
            try (var client = SampleSupport.connectRawTcp(endpoint)) {
                SampleSupport.waitStreamConnected(monitor);
                SampleSupport.sendRawTcp(client, "seed".getBytes());
                RoutingId sessionRid;
                try (systems.zlink.contracts.messaging.Received received = new systems.zlink.contracts.messaging.Received()) {
                    stream.recv(received, systems.zlink.contracts.sockets.RecvFlags.NONE);
                    sessionRid = received.getRoutingId().orElseThrow();
                }
                stream.bindActor(sessionRid, actorRef)
                  .timeout(Duration.ofSeconds(2))
                  .submit()
                  .toCompletableFuture()
                  .join()
                  .forEach(Message::close);
                try (Message request = Message.from("join-first")) {
                    actor.join(spot)
                      .message(request)
                      .timeout(Duration.ofSeconds(2))
                      .submit((result, messages) -> {
                        replies.add(result.result());
                        messages.forEach(Message::close);
                    });
                }
                SampleSupport.waitUntil("actor join", () -> !replies.isEmpty());

                try (Message payload = Message.from("before")) {
                    stream.sendBoundActor(sessionRid, "single-player")
                      .message(payload)
                      .submit();
                }
                actor.leave(spot).submit().toCompletableFuture().join().forEach(Message::close);
                try (Message payload = Message.from("between")) {
                    stream.sendBoundActor(sessionRid, "single-player")
                      .message(payload)
                      .submit();
                }

                try (Message request = Message.from("join-second")) {
                    actor.join(spot)
                      .message(request)
                      .timeout(Duration.ofSeconds(2))
                      .submit((result, messages) -> {
                        replies.add(result.result());
                        messages.forEach(Message::close);
                    });
                }
                SampleSupport.waitUntil("queued actor payload",
                    () -> payloads.size() >= 2);
                if (!List.of("before", "between").equals(payloads)) {
                    throw new IllegalStateException(
                      "queued payloads were not preserved");
                }
                actor.leave(spot).submit().toCompletableFuture().join().forEach(Message::close);
                actor.close();
            }
            System.out.println("[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"");
        }
    }
}
