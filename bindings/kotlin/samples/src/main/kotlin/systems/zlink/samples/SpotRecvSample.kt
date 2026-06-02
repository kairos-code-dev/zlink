// 자립형 가이드 예제: Spot ↔ Spot 토픽 발행/구독을 recv로 받는다.
//   bindings/java/gradlew -p . :kotlin-samples:runSpotRecvSample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.messaging.TopicMessage
import systems.zlink.contracts.sockets.RecvFlags
import java.nio.charset.StandardCharsets
import java.time.Duration
import java.time.Instant

fun main() {
    SampleSupport.ensureNative()
    val channelName = "sample"
    val topic = SampleSupport.SPOT_TOPIC
    val published = "$topic/${SampleSupport.SPOT_PAYLOAD}"
    val publisherEndpoint = SampleSupport.tcpEndpoint()
    val subscriberEndpoint = SampleSupport.tcpEndpoint()

    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { publisherNode ->
            ctx.createSpotNode().use { subscriberNode ->
                publisherNode.createSpot().use { publisher ->
                    subscriberNode.createSpot().use { subscriber ->
                        publisherNode.setRoutingId(
                            RoutingId.from("z-java-spot-recv-publisher".toByteArray(StandardCharsets.UTF_8))
                        )
                        subscriberNode.setRoutingId(
                            RoutingId.from("a-java-spot-recv-subscriber".toByteArray(StandardCharsets.UTF_8))
                        )
                        publisher.setRoutingId(
                            RoutingId.from("z-java-spot-recv-publisher-spot".toByteArray(StandardCharsets.UTF_8))
                        )
                        subscriber.setRoutingId(
                            RoutingId.from("a-java-spot-recv-subscriber-spot".toByteArray(StandardCharsets.UTF_8))
                        )
                        publisherNode.setPubBind(publisherEndpoint)
                        subscriberNode.setPubBind(subscriberEndpoint)
                        publisherNode.connectPeer(subscriberEndpoint)
                        subscriberNode.connectPeer(publisherEndpoint)
                        subscriber.setSubscription(topic)
                        SampleSupport.waitSpotPeerConnected(publisherNode)
                        SampleSupport.waitSpotPeerConnected(subscriberNode)

                        val deadline = Instant.now().plus(Duration.ofSeconds(5))
                        while (Instant.now().isBefore(deadline)) {
                            Message.from(SampleSupport.SPOT_PAYLOAD).use { payload ->
                                publisher.publish(topic).message(payload).submit()
                            }
                            TopicMessage().use { topicMessage ->
                                if (!subscriber.subscribe(topicMessage, RecvFlags.DONT_WAIT)) {
                                    Thread.onSpinWait()
                                    return@use
                                }
                                val value = "${topicMessage.topic()}/${topicMessage.singlePartOrThrow().toUtf8String()}"
                                check(published == value) { "unexpected delivery: $value" }
                                println(
                                    "[spot/recv] service: \"$channelName\" tick: 1 publish: \"$published\"" +
                                        " -> recv: \"$value\""
                                )
                                return
                            }
                        }
                        throw IllegalStateException("spot delivery did not arrive")
                    }
                }
            }
        }
    }
}
