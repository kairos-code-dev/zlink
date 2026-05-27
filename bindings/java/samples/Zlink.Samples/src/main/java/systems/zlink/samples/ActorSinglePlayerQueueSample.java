package systems.zlink.samples;

import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorJoinRequest;
import systems.zlink.contracts.service.spot.ActorPart;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.core.Context;
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

public final class ActorSinglePlayerQueueSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot spot = node.createSpot();
             StreamSocket stream = new StreamSocket(ctx);
             var monitor = stream.monitorOpen(
                 systems.zlink.contracts.eventing.MonitorEventType.ACCEPTED)) {
            Actor actor = node.createActor("solo");
            ActorRef actorRef = actor.ref();
            List<String> payloads = new ArrayList<>();
            List<RequestResult> replies = new ArrayList<>();

            spot.onDispatchEvent(info -> {
                if (info.event() == SpotDispatchEvent.ACTOR_JOIN_READABLE) {
                    try (ActorJoinRequest request =
                             spot.recvActorJoin(RecvFlags.DONT_WAIT)) {
                        if (request != null) {
                            try (Message reply = Message.from("ok")) {
                                spot.replyActorJoin(request, 0)
                                  .message(reply)
                                  .submit();
                            }
                        }
                    }
                    return;
                }
                if (info.event() == SpotDispatchEvent.ACTOR_READABLE) {
                    for (ActorPart part : info.actorParts()) {
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
                    sessionRid = received.routingId().orElseThrow();
                }
                stream.bindActor(sessionRid, actorRef)
                  .timeout(Duration.ofSeconds(2))
                  .submitAsync()
                  .join()
                  .forEach(Message::close);
                try (Message request = Message.from("join")) {
                    actor.join(spot)
                      .message(request)
                      .timeout(Duration.ofSeconds(2))
                      .submit((result, messages) -> {
                        replies.add(result.result());
                        messages.forEach(Message::close);
                    });
                }
                SampleSupport.waitUntil("actor join", () -> !replies.isEmpty());
                actor.leave(spot).submitAsync().join().forEach(Message::close);
                try (Message payload = Message.from("queued")) {
                    stream.sendBoundActor(sessionRid, "solo")
                      .message(payload)
                      .submit();
                }

                try (Message request = Message.from("rejoin")) {
                    actor.join(spot)
                      .message(request)
                      .timeout(Duration.ofSeconds(2))
                      .submit((result, messages) -> {
                        replies.add(result.result());
                        messages.forEach(Message::close);
                    });
                }
                SampleSupport.waitUntil("queued actor payload",
                    () -> !payloads.isEmpty());
                if (!List.of("queued").equals(payloads)) {
                    throw new IllegalStateException(
                      "queued payload was not preserved");
                }
                actor.leave(spot).submitAsync().join().forEach(Message::close);
                actor.close();
            }
            System.out.println("[actor/solo] queued payload preserved across leave");
        }
    }
}
