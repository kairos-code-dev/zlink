package systems.zlink.samples;

import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorJoinRequest;
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
import systems.zlink.contracts.sockets.SpotDispatchEvent;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.sockets.StreamSocket;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

public final class ActorRoomServerSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot spot = node.createSpot();
             StreamSocket stream = ctx.createStreamSocket();
             var monitor = stream.monitorOpen(MonitorEventType.ACCEPTED)) {
            Actor actor = node.createActor("room-player-1");
            ActorRef actorRef = actor.ref();
            List<String> joins = new ArrayList<>();
            List<RequestResult> replies = new ArrayList<>();
            List<String> payloads = new ArrayList<>();

            spot.setDispatchHandler(info -> {
                if (info.event() == SpotDispatchEvent.ACTOR_JOIN_READABLE) {
                    try (ActorJoinRequest request =
                             spot.recvActorJoin(RecvFlags.DONT_WAIT)) {
                        if (request == null) {
                            return;
                        }
                        joins.add(request.message().toUtf8String());
                        try (Message reply = Message.from("accepted")) {
                            spot.replyActorJoin(request, 0)
                              .message(reply)
                              .submit();
                        }
                    }
                } else if (info.event() == SpotDispatchEvent.ACTOR_READABLE) {
                    for (var part : info.actorMessages()) {
                        try (part) {
                            payloads.add(part.message().toUtf8String());
                        }
                    }
                }
            });

            stream.bind(endpoint);
            stream.attachActorGateway(node);
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
                  .submitAsync()
                  .toCompletableFuture()
                  .join()
                  .forEach(Message::close);

                try (Message request = Message.from("enter-room")) {
                    actor.join(spot)
                      .message(request)
                      .timeout(Duration.ofSeconds(2))
                      .submit((result, messages) -> {
                        replies.add(result.result());
                        messages.forEach(Message::close);
                    });
                }
                SampleSupport.waitUntil("actor join", () -> !replies.isEmpty());

                if (!List.of("enter-room").equals(joins)
                    || replies.get(0) != RequestResult.OK
                    || spot.actors().isEmpty()) {
                    throw new IllegalStateException("actor room join failed");
                }

                try (Message inbound = Message.from("move:north")) {
                    stream.sendBoundActor(sessionRid, "room-player-1")
                      .message(inbound)
                      .submit();
                }
                SampleSupport.waitUntil("actor payload",
                    () -> payloads.contains("move:north"));

                actor.leave(spot).submitAsync().toCompletableFuture().join().forEach(Message::close);
                actor.close();
            }
            System.out.println("[actor/room] stream payload: \"move:north\" -> actor: \"move:north\"");
        }
    }
}
