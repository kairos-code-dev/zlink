/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
// 서버가 각 플레이어에게 id로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.
//   bindings/java/gradlew -p . :samples:runActorRoomExample --no-daemon
package systems.zlink.samples;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorJoinRequest;
import systems.zlink.contracts.service.spot.ActorReceived;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.service.spot.SpotDispatchEvent;
import systems.zlink.contracts.sockets.StreamSocket;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;

public final class ActorRoomExample {
    public static void main(String[] args) throws Exception {
// --8<-- [start:doc]
        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot room = node.createSpot();
             StreamSocket stream = ctx.createStreamSocket()) {
            Actor player1 = node.createActor("player-1");
            Actor player2 = node.createActor("player-2");
            List<String> received = new ArrayList<>();

            RoutingId session = RoutingId.from("game-room-session");
            stream.bindActor(session, player1.ref()).submit().toCompletableFuture().join().forEach(Message::close);
            stream.bindActor(session, player2.ref()).submit().toCompletableFuture().join().forEach(Message::close);

            // dispatch 핸들러: join 요청을 수락하고, 도착한 메시지를 모은다.
            room.setDispatchHandler(info -> {
                if (info.event() == SpotDispatchEvent.ACTOR_JOIN_READABLE) {
                    try (ActorJoinRequest request = room.recvActorJoin(RecvFlags.DONT_WAIT)) {
                        if (request != null) {
                            try (Message reply = Message.from("accepted")) {
                                room.replyActorJoin(request, 0).message(reply).submit();
                            }
                        }
                    }
                } else if (info.event() == SpotDispatchEvent.ACTOR_READABLE) {
                    for (ActorReceived part : info.actorMessages()) {
                        try (part) {
                            received.add(part.message().toUtf8String());
                        }
                    }
                }
            });

            join(player1, room);
            join(player2, room);

            // 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
            sendAndWait(stream, session, "player-1", "your-turn", received, 1);
            sendAndWait(stream, session, "player-2", "wait", received, 2);

            if (!List.of("your-turn", "wait").equals(received)) {
                throw new IllegalStateException("messages were not routed per actor: " + received);
            }

            player1.leave(room).submit().toCompletableFuture().join().forEach(Message::close);
            player2.leave(room).submit().toCompletableFuture().join().forEach(Message::close);
            player1.close();
            player2.close();
            System.out.println("[actor/room] player-1: \"your-turn\", player-2: \"wait\"");
        }
// --8<-- [end:doc]
    }

    private static void join(Actor actor, Spot room) throws InterruptedException {
        CountDownLatch done = new CountDownLatch(1);
        try (Message m = Message.from("enter-room")) {
            actor.join(room).message(m).timeout(Duration.ofSeconds(2))
                .submit((result, messages) -> {
                    messages.forEach(Message::close);
                    done.countDown();
                });
        }
        done.await();
    }

    // 보낸 직후 도착을 기다리므로, 그 메시지는 방금 주소 지정한 플레이어 것이다.
    private static void sendAndWait(StreamSocket stream, RoutingId session, String actorId,
                                    String text, List<String> received, int want)
            throws InterruptedException {
        try (Message m = Message.from(text)) {
            stream.sendBoundActor(session, actorId).message(m).submit();
        }
        long deadline = System.nanoTime() + Duration.ofSeconds(2).toNanos();
        while (received.size() < want && System.nanoTime() < deadline) {
            Thread.sleep(10);
        }
        if (received.size() < want) {
            throw new IllegalStateException("message to " + actorId + " not delivered");
        }
    }
}
