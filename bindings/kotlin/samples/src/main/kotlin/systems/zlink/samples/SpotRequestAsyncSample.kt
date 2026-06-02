// 자립형 가이드 예제: SPOT → 채널 비동기 요청/응답 (콜백).
//   bindings/java/gradlew -p . :kotlin-samples:runSpotRequestAsyncSample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.messaging.Received
import systems.zlink.contracts.sockets.RecvFlags
import systems.zlink.contracts.sockets.RequestResult
import java.time.Duration
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

fun main() {
    SampleSupport.ensureNative()
    val endpoint = SampleSupport.tcpEndpoint()
    val channelName = "orders"

    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { requesterNode ->
            ctx.createDealerSocket().use { requesterDealer ->
                ctx.createRouterSocket().use { requesterRouter ->
                    requesterNode.createSpot().use { requester ->
                        requesterRouter.bind(endpoint)
                        requesterDealer.connect(endpoint)
                        requesterNode.attachChannelDealerManual(channelName, requesterDealer)

                        val responder = Thread({
                            Received().use { received ->
                                requesterRouter.recv(received, RecvFlags.NONE)
                                check("spot-ping" == SampleSupport.singleUtf8(received)) {
                                    "unexpected spot request"
                                }
                                Message.from("spot-pong").use { reply ->
                                    received.reply().message(reply).submit()
                                }
                            }
                        }, "spot-request-async-responder")
                        responder.start()

                        val replyLatch = CountDownLatch(1)
                        var replyHolder: List<Message> = emptyList()
                        var resultHolder = RequestResult.TIMED_OUT
                        Message.from("spot-ping").use { request ->
                            requester.requestToChannel(channelName)
                                .message(request)
                                .timeout(Duration.ofSeconds(5))
                                .submit { requestResult, replyParts ->
                                    resultHolder = requestResult
                                    replyHolder = replyParts
                                    replyLatch.countDown()
                                }
                        }
                        check(replyLatch.await(5, TimeUnit.SECONDS)) {
                            "spot request async callback timed out"
                        }
                        check(resultHolder == RequestResult.OK) {
                            "unexpected request result $resultHolder"
                        }
                        val reply = replyHolder
                        try {
                            check(reply.size == 1 && "spot-pong" == reply[0].toUtf8String()) {
                                "unexpected spot reply"
                            }
                        } finally {
                            reply.forEach(Message::close)
                        }

                        responder.join(TimeUnit.SECONDS.toMillis(5))
                        println("[spot/request/async] request: \"spot-ping\" -> reply: \"spot-pong\"")
                    }
                }
            }
        }
    }
}
