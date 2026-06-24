// 자립형 가이드 예제: SPOT → 채널(ROUTER→ROUTER) 요청.
// 게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
//   bindings/java/gradlew -p . :kotlin-samples:runSpotChannelExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.messaging.Received
import systems.zlink.contracts.sockets.RecvFlags
import java.time.Duration

fun main() {
// --8<-- [start:doc]
    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { roomNode ->
            roomNode.createSpot().use { room ->
                ctx.createRouterSocket().use { roomRouter ->
                    ctx.createRouterSocket().use { apiRouter ->
                        roomNode.createRouteBridge().use { bridge ->
                            val channel = "api"
                            val endpoint = SampleSupport.tcpEndpoint()
                            val roomRouterRid = RoutingId.from("room-channel-client")
                            val apiRouterRid = RoutingId.from("room-channel-server")
                            roomRouter.setRoutingId(roomRouterRid)
                            apiRouter.setRoutingId(apiRouterRid)
                            apiRouter.bind(endpoint)
                            roomRouter.connect(endpoint)
                            // "api" 채널 호출을 이 ROUTER로 내보내도록 bridge에 등록한다.
                            bridge.attachRouterChannel(channel, roomRouter)

                            // 게임룸이 API 채널로 outgame 요청을 보낸다.
                            val replyFuture = bridge.request(channel, apiRouterRid, room.routingId)
                                .message(Message.from("get-profile"))
                                .timeout(Duration.ofSeconds(5))
                                .submit()

                            // API 서버(ROUTER)는 요청을 폴링으로 받아 응답한다.
                            Received().use { received ->
                                val deadline = System.nanoTime() + Duration.ofSeconds(5).toNanos()
                                var served = false
                                while (!served && System.nanoTime() < deadline) {
                                    try {
                                        if (apiRouter.recv(received, RecvFlags.DONT_WAIT)) {
                                            Message.from("profile:level-7").use { reply ->
                                                received.reply().message(reply).submit()
                                            }
                                            served = true
                                        }
                                    } catch (noData: RuntimeException) {
                                        // no data yet
                                    }
                                    if (!served) Thread.sleep(10)
                                }
                            }

                            val reply = replyFuture.toCompletableFuture().join()
                            println("[spot/channel] request \"get-profile\" -> reply \"${reply[0].toUtf8String()}\"")
                            reply.forEach(Message::close)
                        }
                    }
                }
            }
        }
    }
// --8<-- [end:doc]
}
