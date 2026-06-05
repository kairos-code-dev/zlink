/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
// 한 파일 안에 전체 흐름이 들어 있다 — 별도 헬퍼 없이 빌드·실행된다:
//   bindings/java/gradlew -p . :samples:runActorQueueExample --no-daemon
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

public final class ActorQueueExample {
    public static void main(String[] args) throws Exception {
        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode();
             Spot spot = node.createSpot();
             StreamSocket stream = ctx.createStreamSocket()) {
            Actor actor = node.createActor("single-player");
            List<String> payloads = new ArrayList<>();

            // 스트림 게이트웨이에 actor를 세션으로 바인딩한다. 실제 서버에서 session은
            // 게이트웨이로 접속한 클라이언트의 라우팅 ID다 — 여기선 고정값으로 만든다.
            stream.attachActorGateway(node);
            RoutingId session = RoutingId.from("single-player-session");
            stream.bindActor(session, actor.ref())
                .timeout(Duration.ofSeconds(2)).submitAsync().toCompletableFuture().join().forEach(Message::close);

            // dispatch 핸들러: join 요청을 수락하고, actor에게 온 메시지를 모은다.
            spot.setDispatchHandler(info -> {
                if (info.event() == SpotDispatchEvent.ACTOR_JOIN_READABLE) {
                    try (ActorJoinRequest request = spot.recvActorJoin(RecvFlags.DONT_WAIT)) {
                        if (request != null) {
                            try (Message reply = Message.from("accepted")) {
                                spot.replyActorJoin(request, 0).message(reply).submit();
                            }
                        }
                    }
                } else if (info.event() == SpotDispatchEvent.ACTOR_READABLE) {
                    for (ActorReceived part : info.actorMessages()) {
                        try (part) {
                            payloads.add(part.message().toUtf8String());
                        }
                    }
                }
            });

            join(actor, spot, "join-first");                          // actor가 spot에 합류
            send(stream, session, "before");                          // joined 상태에서 도착
            actor.leave(spot).submitAsync().toCompletableFuture().join().forEach(Message::close); // 처리 위치 이탈
            send(stream, session, "between");                         // leave 사이에 도착 → 큐잉
            join(actor, spot, "join-second");                         // rejoin → 큐된 메시지 배달

            long deadline = System.nanoTime() + Duration.ofSeconds(2).toNanos();
            while (payloads.size() < 2 && System.nanoTime() < deadline) {
                Thread.sleep(10);
            }
            if (!List.of("before", "between").equals(payloads)) {
                throw new IllegalStateException("queued payloads were not preserved: " + payloads);
            }

            actor.leave(spot).submitAsync().toCompletableFuture().join().forEach(Message::close);
            actor.close();
            System.out.println(
                "[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"");
        }
    }

    private static void join(Actor actor, Spot spot, String payload) throws InterruptedException {
        CountDownLatch done = new CountDownLatch(1);
        try (Message m = Message.from(payload)) {
            actor.join(spot).message(m).timeout(Duration.ofSeconds(2))
                .submit((result, messages) -> {
                    messages.forEach(Message::close);
                    done.countDown();
                });
        }
        done.await();
    }

    private static void send(StreamSocket stream, RoutingId session, String payload) {
        try (Message m = Message.from(payload)) {
            stream.sendBoundActor(session, "single-player").message(m).submit();
        }
    }
}
