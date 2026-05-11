package systems.zlink.samples;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.RequestCallback;
import systems.zlink.MonitorEventType;
import systems.zlink.RecvFlags;
import systems.zlink.RequestResult;
import systems.zlink.RoutingId;
import systems.zlink.SpotDispatchEvent;
import systems.zlink.StreamSocket;
import systems.zlink.service.spot.Actor;
import systems.zlink.service.spot.ActorJoinRequest;
import systems.zlink.service.spot.ActorRef;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
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
                        spot.replyActorJoin(request, true, reply);
                    }
                }
            });

            stream.bind(endpoint);
            try (var client = SampleSupport.connectRawTcp(endpoint)) {
                SampleSupport.waitStreamConnected(monitor);
                SampleSupport.sendRawTcp(client, "seed".getBytes());
                RoutingId sessionRid;
                try (systems.zlink.Received received = new systems.zlink.Received()) {
                    stream.recv(received, systems.zlink.RecvFlags.NONE);
                    sessionRid = received.routingId().orElseThrow();
                }
                stream.bindActor(node, sessionRid, actorRef,
                  Duration.ofSeconds(2));

                try (Message request = Message.copyOfUtf8("join-room")) {
                    actor.join(spot, request, (RequestCallback) (result, messages) -> {
                        replies.add(result);
                        messages.forEach(Message::close);
                    }, Duration.ofSeconds(2));
                }
                SampleSupport.waitUntil("actor join", () -> !replies.isEmpty());

                if (!List.of("join-room").equals(joins)
                    || replies.get(0) != RequestResult.OK
                    || spot.actorsSnapshot().isEmpty()) {
                    throw new IllegalStateException("actor room join failed");
                }
                actor.leave(spot);
                actor.close();
            }
            System.out.println("[actor/room] join accepted");
        }
    }
}
