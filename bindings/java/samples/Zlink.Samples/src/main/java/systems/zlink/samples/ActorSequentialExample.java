/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.
// Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
// 옮겨 간다. 메시지는 STREAM session에 actor를 bind하고 packet을 relay해야만
// 도달하며, room의 dispatch context에서 들어온 순서대로 처리된다.
//   bindings/java/gradlew -p . :samples:runActorSequentialExample --no-daemon
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

public final class ActorSequentialExample {
    public static void main(String[] args) throws Exception {
// --8<-- [start:doc]
        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot room = node.createSpot();
             StreamSocket stream = ctx.createStreamSocket()) {
            // 생성 직후 actor는 Entry Spot(로비)에 위치한다.
            Actor player = node.createActor("player");
            List<String> processed = new ArrayList<>();

            stream.attachActorGateway(node);
            RoutingId session = RoutingId.from("player-session");
            // STREAM session에 actor를 bind한다 (이후 relay가 이 actor로 간다).
            stream.bindActor(session, player.ref()).submitAsync().toCompletableFuture().join().forEach(Message::close);

            // dispatch 핸들러: join을 수락하고, STREAM이 relay한 메시지를 모은다.
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
                            processed.add(part.message().toUtf8String());
                        }
                    }
                }
            });

            // join으로 Entry Spot에서 room(user Spot)으로 이동한다.
            CountDownLatch joined = new CountDownLatch(1);
            try (Message m = Message.from("enter-room")) {
                player.join(room).message(m).timeout(Duration.ofSeconds(2))
                    .submit((result, messages) -> {
                        messages.forEach(Message::close);
                        joined.countDown();
                    });
            }
            joined.await();

            // STREAM이 플레이어 입력을 연달아 relay한다 — actor는 순서대로 처리한다.
            List<String> commands = List.of("move", "attack", "loot");
            for (String command : commands) {
                try (Message m = Message.from(command)) {
                    stream.sendBoundActor(session, "player").message(m).submit();
                }
            }

            long deadline = System.nanoTime() + Duration.ofSeconds(2).toNanos();
            while (processed.size() < commands.size() && System.nanoTime() < deadline) {
                Thread.sleep(10);
            }
            if (!commands.equals(processed)) {
                throw new IllegalStateException("messages were not processed in order: " + processed);
            }

            player.leave(room).submitAsync().toCompletableFuture().join().forEach(Message::close);
            player.close();
            System.out.println("[actor/sequential] processed in order: move -> attack -> loot");
        }
// --8<-- [end:doc]
    }
}
