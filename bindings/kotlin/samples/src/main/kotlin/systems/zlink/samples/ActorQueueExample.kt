// 자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
// 한 파일 안에 전체 흐름이 들어 있다 — 별도 헬퍼 없이 빌드·실행된다:
//   bindings/java/gradlew -p . :kotlin-samples:runActorQueueExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.sockets.RecvFlags
import systems.zlink.contracts.sockets.SpotDispatchEvent
import java.time.Duration
import java.util.concurrent.CountDownLatch

fun main() {
    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { node ->
            node.createSpot().use { spot ->
                ctx.createStreamSocket().use { stream ->
                    val actor = node.createActor("single-player")
                    val payloads = mutableListOf<String>()

                    // 스트림 게이트웨이에 actor를 세션으로 바인딩한다. 실제 서버에서
                    // session은 게이트웨이로 접속한 클라이언트의 라우팅 ID다 —
                    // 여기선 고정값으로 만든다.
                    stream.attachActorGateway(node)
                    val session = RoutingId.from("single-player-session")
                    stream.bindActor(session, actor.ref())
                        .timeout(Duration.ofSeconds(2)).submitAsync().join().forEach(Message::close)

                    // dispatch 핸들러: join 요청을 수락하고, actor에게 온 메시지를 모은다.
                    spot.setDispatchHandler { info ->
                        when (info.event()) {
                            SpotDispatchEvent.ACTOR_JOIN_READABLE ->
                                spot.recvActorJoin(RecvFlags.DONT_WAIT)?.use { request ->
                                    Message.from("accepted").use { reply ->
                                        spot.replyActorJoin(request, 0).message(reply).submit()
                                    }
                                }
                            SpotDispatchEvent.ACTOR_READABLE ->
                                for (part in info.actorMessages()) {
                                    part.use { payloads.add(it.message().toUtf8String()) }
                                }
                            else -> {}
                        }
                    }

                    fun join(payload: String) {
                        val done = CountDownLatch(1)
                        Message.from(payload).use { m ->
                            actor.join(spot).message(m).timeout(Duration.ofSeconds(2)).submit { _, messages ->
                                messages.forEach(Message::close)
                                done.countDown()
                            }
                        }
                        done.await()
                    }
                    fun send(payload: String) {
                        Message.from(payload).use { m ->
                            stream.sendBoundActor(session, "single-player").message(m).submit()
                        }
                    }

                    join("join-first")  // actor가 spot에 합류
                    send("before")      // joined 상태에서 도착
                    actor.leave(spot).submitAsync().join().forEach(Message::close) // 처리 위치 이탈
                    send("between")     // leave 사이에 도착 → 큐잉
                    join("join-second") // rejoin → 큐된 메시지가 핸들러로 배달된다

                    val deadline = System.nanoTime() + Duration.ofSeconds(2).toNanos()
                    while (payloads.size < 2 && System.nanoTime() < deadline) {
                        Thread.sleep(10)
                    }
                    check(payloads == listOf("before", "between")) {
                        "queued payloads were not preserved: $payloads"
                    }

                    actor.leave(spot).submitAsync().join().forEach(Message::close)
                    actor.close()
                    println("[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"")
                }
            }
        }
    }
}
