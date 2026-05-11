package systems.zlink.samples;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.RequestCallback;
import systems.zlink.RecvFlags;
import systems.zlink.RequestResult;
import systems.zlink.RoutingId;
import systems.zlink.SpotDispatchEvent;
import systems.zlink.StreamSocket;
import systems.zlink.service.spot.Actor;
import systems.zlink.service.spot.ActorJoinRequest;
import systems.zlink.service.spot.ActorPart;
import systems.zlink.service.spot.ActorRef;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

public final class ActorGatewayRelaySample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot spot = node.createSpot();
             StreamSocket stream = new StreamSocket(ctx);
             var monitor = stream.monitorOpen(
                 systems.zlink.MonitorEventType.ACCEPTED)) {
            Actor actor = node.createActor("player-1");
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
                SampleSupport.sendRawTcp(client, "hello".getBytes());
                RoutingId sessionRid;
                try (systems.zlink.Received received = new systems.zlink.Received()) {
                    stream.recv(received, systems.zlink.RecvFlags.NONE);
                    sessionRid = received.routingId().orElseThrow();
                }
                stream.bindActor(node, sessionRid, actorRef, Duration.ofSeconds(2));
                try (Message request = Message.copyOfUtf8("join")) {
                    actor.join(spot, request, (RequestCallback) (result, messages) -> {
                        replies.add(result);
                        messages.forEach(Message::close);
                    }, Duration.ofSeconds(2));
                }
                SampleSupport.waitUntil("actor join", () -> !replies.isEmpty());
                try (Message payload = Message.copyOfUtf8("client-payload")) {
                    stream.sendBoundActor(node, sessionRid, "player-1", payload);
                }
                SampleSupport.waitUntil("actor payload",
                    () -> !payloads.isEmpty());

                if (!List.of("client-payload").equals(payloads)) {
                    throw new IllegalStateException("unexpected actor payload");
                }
                actor.leave(spot);
                actor.close();
            }
            System.out.println("[actor/gateway] stream relayed to actor");
        }
    }
}
