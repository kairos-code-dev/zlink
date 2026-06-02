// 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
// 서버가 각 플레이어에게 id로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.
//   bindings/java/gradlew -p . :kotlin-samples:runActorRoomExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.service.spot.Actor
import systems.zlink.contracts.service.spot.Spot
import systems.zlink.contracts.sockets.RecvFlags
import systems.zlink.contracts.sockets.SpotDispatchEvent
import systems.zlink.contracts.sockets.StreamSocket
import java.time.Duration
import java.util.concurrent.CountDownLatch

private fun join(actor: Actor, room: Spot) {
    val done = CountDownLatch(1)
    Message.from("enter-room").use { m ->
        actor.join(room).message(m).timeout(Duration.ofSeconds(2)).submit { _, messages ->
            messages.forEach(Message::close)
            done.countDown()
        }
    }
    done.await()
}

// 보낸 직후 도착을 기다리므로, 그 메시지는 방금 주소 지정한 플레이어 것이다.
private fun sendAndWait(
    stream: StreamSocket, session: RoutingId, actorId: String, text: String,
    received: List<String>, want: Int
) {
    Message.from(text).use { m ->
        stream.sendBoundActor(session, actorId).message(m).submit()
    }
    val deadline = System.nanoTime() + Duration.ofSeconds(2).toNanos()
    while (received.size < want && System.nanoTime() < deadline) {
        Thread.sleep(10)
    }
    check(received.size >= want) { "message to $actorId not delivered" }
}

fun main() {
// --8<-- [start:doc]
    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { node ->
            node.createSpot().use { room ->
                ctx.createStreamSocket().use { stream ->
                    val player1 = node.createActor("player-1")
                    val player2 = node.createActor("player-2")
                    val received = mutableListOf<String>()

                    stream.attachActorGateway(node)
                    val session = RoutingId.from("game-room-session")
                    stream.bindActor(session, player1.ref()).submitAsync().toCompletableFuture().join().forEach(Message::close)
                    stream.bindActor(session, player2.ref()).submitAsync().toCompletableFuture().join().forEach(Message::close)

                    // dispatch 핸들러: join 요청을 수락하고, 도착한 메시지를 모은다.
                    room.setDispatchHandler { info ->
                        when (info.event()) {
                            SpotDispatchEvent.ACTOR_JOIN_READABLE ->
                                room.recvActorJoin(RecvFlags.DONT_WAIT)?.use { request ->
                                    Message.from("accepted").use { reply ->
                                        room.replyActorJoin(request, 0).message(reply).submit()
                                    }
                                }
                            SpotDispatchEvent.ACTOR_READABLE ->
                                for (part in info.actorMessages()) {
                                    part.use { received.add(it.message().toUtf8String()) }
                                }
                            else -> {}
                        }
                    }

                    join(player1, room)
                    join(player2, room)

                    // 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
                    sendAndWait(stream, session, "player-1", "your-turn", received, 1)
                    sendAndWait(stream, session, "player-2", "wait", received, 2)

                    check(received == listOf("your-turn", "wait")) {
                        "messages were not routed per actor: $received"
                    }

                    player1.leave(room).submitAsync().toCompletableFuture().join().forEach(Message::close)
                    player2.leave(room).submitAsync().toCompletableFuture().join().forEach(Message::close)
                    player1.close()
                    player2.close()
                    println("[actor/room] player-1: \"your-turn\", player-2: \"wait\"")
                }
            }
        }
    }
// --8<-- [end:doc]
}
