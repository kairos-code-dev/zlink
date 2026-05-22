package systems.zlink.samples;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.MonitorEventType;
import systems.zlink.contracts.RecvFlags;
import systems.zlink.contracts.RequestResult;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.SpotDispatchEvent;
import systems.zlink.contracts.StreamSocket;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorJoinRequest;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

public final class ActorRoomServerSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot spot = node.createSpot();
             StreamSocket stream = new StreamSocket(ctx);
             var monitor = stream.monitorOpen(MonitorEventType.ACCEPTED)) {
            Actor actor = node.createActor("room-1");
            ActorRef actorRef = actor.ref();
            List<String> joins = new ArrayList<>();
            List<RequestResult> replies = new ArrayList<>();

            spot.onDispatchEvent(info -> {
                if (info.event() != SpotDispatchEvent.ACTOR_JOIN_READABLE) {
                    return;
                }
                try (ActorJoinRequest request =
                         spot.recvActorJoin(RecvFlags.DONT_WAIT)) {
                    if (request == null) {
                        return;
                    }
                    joins.add(request.message().toUtf8String());
                    try (Message reply = Message.copyOfUtf8("joined")) {
                        spot.replyActorJoin(request, true)
                          .message(reply)
                          .submit();
                    }
                }
            });

            stream.bind(endpoint);
            stream.attachActorGateway(node);
            try (var client = SampleSupport.connectRawTcp(endpoint)) {
                SampleSupport.waitStreamConnected(monitor);
                SampleSupport.sendRawTcp(client, "seed".getBytes());
                RoutingId sessionRid;
                try (systems.zlink.contracts.Received received = new systems.zlink.contracts.Received()) {
                    stream.recv(received, systems.zlink.contracts.RecvFlags.NONE);
                    sessionRid = received.routingId().orElseThrow();
                }
                stream.bindActor(sessionRid, actorRef)
                  .timeout(Duration.ofSeconds(2))
                  .submitAsync()
                  .join()
                  .forEach(Message::close);

                try (Message request = Message.copyOfUtf8("join-room")) {
                    actor.join(spot)
                      .message(request)
                      .timeout(Duration.ofSeconds(2))
                      .submit((result, messages) -> {
                        replies.add(result.result());
                        messages.forEach(Message::close);
                    });
                }
                SampleSupport.waitUntil("actor join", () -> !replies.isEmpty());

                if (!List.of("join-room").equals(joins)
                    || replies.get(0) != RequestResult.OK
                    || spot.actorsSnapshot().isEmpty()) {
                    throw new IllegalStateException("actor room join failed");
                }
                actor.leave(spot).submitAsync().join().forEach(Message::close);
                actor.close();
            }
            System.out.println("[actor/room] join accepted");
        }
    }
}
