package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RequestResult;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.StreamSocket;
import dev.kairoscode.zlink.service.spot.Actor;
import dev.kairoscode.zlink.service.spot.ActorJoinRequest;
import dev.kairoscode.zlink.service.spot.ActorRef;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
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
            Actor actor = node.actor("room-1");
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
                try (var received = stream.recv()) {
                    sessionRid = received.routingId().orElseThrow();
                }
                stream.bindActor(node, sessionRid, actorRef,
                  Duration.ofSeconds(2));

                try (Message request = Message.copyOfUtf8("join-room")) {
                    actor.join(spot, request, (result, messages) -> {
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
