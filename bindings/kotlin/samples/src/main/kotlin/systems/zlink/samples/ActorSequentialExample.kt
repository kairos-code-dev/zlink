// 자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.
// Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
// 옮겨 간다. 메시지는 STREAM session에 actor를 bind하고 packet을 relay해야만
// 도달하며, room의 dispatch context에서 들어온 순서대로 처리된다.
//   bindings/java/gradlew -p . :kotlin-samples:runActorSequentialExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.sockets.RecvFlags
import systems.zlink.contracts.service.spot.SpotDispatchEvent
import java.time.Duration
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

fun main() {
// --8<-- [start:doc]
    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { node ->
            node.createSpot().use { room ->
                ctx.createStreamSocket().use { stream ->
                    // 생성 직후 actor는 Entry Spot(로비)에 위치한다.
                    val player = node.createActor("player")
                    val processed = mutableListOf<String>()

                    stream.attachActorGateway(node)
                    val session = RoutingId.from("player-session")
                    // STREAM session에 actor를 bind한다 (이후 relay가 이 actor로 간다).
                    stream.bindActor(session, player.ref()).timeout(Duration.ofSeconds(2))
                        .submit().toCompletableFuture().join().forEach(Message::close)

                    // dispatch 핸들러: STREAM이 relay한 메시지를 모은다.
                    room.setDispatchHandler { info ->
                        if (info.event() == SpotDispatchEvent.ACTOR_READABLE) {
                            for (part in info.actorMessages()) {
                                part.use { processed.add(it.message().toUtf8String()) }
                            }
                        }
                    }

                    fun acceptJoin() {
                        val deadline = System.nanoTime() + Duration.ofSeconds(2).toNanos()
                        while (System.nanoTime() < deadline) {
                            room.recvActorJoin(RecvFlags.DONT_WAIT)?.use { request ->
                                Message.from("accepted").use { reply ->
                                    room.replyActorJoin(request, 0).message(reply).submit()
                                }
                                return
                            }
                            Thread.sleep(10)
                        }
                        error("join request not received")
                    }

                    // join으로 Entry Spot에서 room(user Spot)으로 이동한다.
                    val joined = CountDownLatch(1)
                    Message.from("enter-room").use { m ->
                        player.join(room).message(m).timeout(Duration.ofSeconds(2)).submit { _, messages ->
                            messages.forEach(Message::close)
                            joined.countDown()
                        }
                    }
                    acceptJoin()
                    check(joined.await(2, TimeUnit.SECONDS)) { "actor join did not complete" }

                    // STREAM이 플레이어 입력을 연달아 relay한다 — actor는 순서대로 처리한다.
                    val commands = listOf("move", "attack", "loot")
                    for (command in commands) {
                        Message.from(command).use { m ->
                            stream.sendBoundActor(session, "player").message(m).submit()
                        }
                    }

                    val deadline = System.nanoTime() + Duration.ofSeconds(2).toNanos()
                    while (processed.size < commands.size && System.nanoTime() < deadline) {
                        Thread.sleep(10)
                    }
                    check(processed == commands) {
                        "messages were not processed in order: $processed"
                    }

                    player.leave(room).timeout(Duration.ofSeconds(2))
                        .submit().toCompletableFuture().join().forEach(Message::close)
                    player.close()
                    println("[actor/sequential] processed in order: move -> attack -> loot")
                }
            }
        }
    }
// --8<-- [end:doc]
}
