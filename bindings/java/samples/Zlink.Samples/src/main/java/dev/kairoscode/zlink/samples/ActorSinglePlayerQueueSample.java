package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RequestResult;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.StreamSocket;
import dev.kairoscode.zlink.service.spot.Actor;
import dev.kairoscode.zlink.service.spot.ActorJoinRequest;
import dev.kairoscode.zlink.service.spot.ActorPart;
import dev.kairoscode.zlink.service.spot.ActorRef;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
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
                 dev.kairoscode.zlink.MonitorEventType.ACCEPTED)) {
            Actor actor = node.actor("solo");
            ActorRef actorRef = actor.ref();
            List<String> payloads = new ArrayList<>();
            List<RequestResult> replies = new ArrayList<>();

            spot.onDispatchEvent(info -> {
                if (info.event() == SpotDispatchEvent.ACTOR_JOIN_READABLE) {
                    try (ActorJoinRequest request =
                             spot.recvActorJoin(RecvFlags.DONT_WAIT)) {
                        if (request != null) {
                            try (Message reply = Message.copyOfUtf8("ok")) {
                                spot.replyActorJoin(request, true, reply);
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
            try (var client = SampleSupport.connectRawTcp(endpoint)) {
                SampleSupport.waitStreamConnected(monitor);
                SampleSupport.sendRawTcp(client, "seed".getBytes());
                RoutingId sessionRid;
                try (var received = stream.recv()) {
                    sessionRid = received.routingId().orElseThrow();
                }
                stream.bindActor(node, sessionRid, actorRef, Duration.ofSeconds(2));
                try (Message request = Message.copyOfUtf8("join")) {
                    actor.join(spot, request, (result, messages) -> {
                        replies.add(result);
                        messages.forEach(Message::close);
                    }, Duration.ofSeconds(2));
                }
                SampleSupport.waitUntil("actor join", () -> !replies.isEmpty());
                actor.leave(spot);
                try (Message payload = Message.copyOfUtf8("queued")) {
                    stream.sendBoundActor(node, sessionRid, "solo", payload);
                }

                try (Message request = Message.copyOfUtf8("rejoin")) {
                    actor.join(spot, request, (result, messages) -> {
                        replies.add(result);
                        messages.forEach(Message::close);
                    }, Duration.ofSeconds(2));
                }
                SampleSupport.waitUntil("queued actor payload",
                    () -> !payloads.isEmpty());
                if (!List.of("queued").equals(payloads)) {
                    throw new IllegalStateException(
                      "queued payload was not preserved");
                }
                actor.leave(spot);
                actor.close();
            }
            System.out.println("[actor/solo] queued payload preserved across leave");
        }
    }
}
