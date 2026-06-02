// 자립형 가이드 예제: SPOT 토픽 pub/sub.
// 한 노드가 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
//   bindings/java/gradlew -p . :kotlin-samples:runSpotPubSubExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.messaging.TopicMessage
import systems.zlink.contracts.service.spot.SpotNode
import systems.zlink.contracts.sockets.RecvFlags
import java.net.InetAddress
import java.net.ServerSocket
import java.time.Duration

private fun uniqueTcp(): String =
    ServerSocket(0, 0, InetAddress.getByName("127.0.0.1")).use { "tcp://127.0.0.1:${it.localPort}" }

private fun waitPeer(node: SpotNode) {
    val deadline = System.nanoTime() + Duration.ofSeconds(15).toNanos()
    while (System.nanoTime() < deadline) {
        if (node.status().connectedPeerCount() > 0) return
        Thread.sleep(10)
    }
    throw IllegalStateException("spot peer not connected")
}

fun main() {
// --8<-- [start:doc]
    val topic = "room:lobby"
    val pubEndpoint = uniqueTcp()
    val subEndpoint = uniqueTcp()
    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { publisherNode ->
            ctx.createSpotNode().use { subscriberNode ->
                publisherNode.createSpot().use { publisher ->
                    subscriberNode.createSpot().use { subscriber ->
                        publisherNode.setPubBind(pubEndpoint)
                        subscriberNode.setPubBind(subEndpoint)
                        publisherNode.connectPeer(subEndpoint)
                        subscriberNode.connectPeer(pubEndpoint)
                        // 구독자는 받을 토픽을 등록한다.
                        subscriber.setSubscription(topic)
                        waitPeer(publisherNode)
                        waitPeer(subscriberNode)

                        // 도착할 때까지 반복 발행한다.
                        var receivedTopic: String? = null
                        var payload: String? = null
                        val deadline = System.nanoTime() + Duration.ofSeconds(5).toNanos()
                        while (System.nanoTime() < deadline) {
                            Message.from("hello-everyone").use { m ->
                                publisher.publish(topic).message(m).submit()
                            }
                            TopicMessage().use { tm ->
                                if (subscriber.subscribe(tm, RecvFlags.DONT_WAIT)) {
                                    receivedTopic = tm.topic()
                                    payload = tm.singlePartOrThrow().toUtf8String()
                                }
                            }
                            if (payload != null) break
                            Thread.sleep(10)
                        }
                        checkNotNull(payload) { "spot delivery did not arrive" }
                        println("[spot/pubsub] topic \"$receivedTopic\" -> recv: \"$payload\"")
                    }
                }
            }
        }
    }
// --8<-- [end:doc]
}
