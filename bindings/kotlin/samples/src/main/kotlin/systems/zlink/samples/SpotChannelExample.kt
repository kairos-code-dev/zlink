// 자립형 가이드 예제: SPOT → 채널(DEALER→ROUTER) 요청.
// 게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
//   bindings/java/gradlew -p . :kotlin-samples:runSpotChannelExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.messaging.Received
import systems.zlink.contracts.sockets.RecvFlags
import java.net.InetAddress
import java.net.ServerSocket
import java.time.Duration

private fun uniqueTcp(): String =
    ServerSocket(0, 0, InetAddress.getByName("127.0.0.1")).use { "tcp://127.0.0.1:${it.localPort}" }

fun main() {
    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { roomNode ->
            roomNode.createSpot().use { room ->
                ctx.createDealerSocket().use { roomDealer ->
                    ctx.createRouterSocket().use { apiRouter ->
                        val channel = "api"
                        val endpoint = uniqueTcp()
                        apiRouter.bind(endpoint)
                        roomDealer.connect(endpoint)
                        // "api" 채널 호출을 이 DEALER로 내보내도록 노드에 등록한다.
                        roomNode.attachChannelDealerManual(channel, roomDealer)

                        // 게임룸이 API 채널로 outgame 요청을 보낸다.
                        val replyFuture = room.requestToChannel(channel)
                            .message(Message.from("get-profile"))
                            .timeout(Duration.ofSeconds(5))
                            .submitAsync()

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

                        val reply = replyFuture.join()
                        println("[spot/channel] request \"get-profile\" -> reply \"${reply[0].toUtf8String()}\"")
                        reply.forEach(Message::close)
                    }
                }
            }
        }
    }
}
