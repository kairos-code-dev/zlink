package systems.zlink.samples

import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.eventing.MonitorEventType
import systems.zlink.contracts.messaging.Message
import systems.zlink.contracts.messaging.Received
import systems.zlink.contracts.sockets.RecvFlags
import systems.zlink.contracts.sockets.RequestResult
import systems.zlink.contracts.sockets.SpotDispatchEvent
import java.time.Duration

fun main() {
    SampleSupport.ensureNative()
    val endpoint = SampleSupport.tcpEndpoint()

    Zlink.createContext().use { ctx ->
        ctx.createSpotNode().use { node ->
            node.createSpot().use { spot ->
                ctx.createStreamSocket().use { stream ->
                    stream.monitorOpen(MonitorEventType.ACCEPTED).use { monitor ->
                        val actor = node.createActor("single-player")
                        val actorRef = actor.ref()
                        val payloads = mutableListOf<String>()
                        val replies = mutableListOf<RequestResult>()

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

                        stream.bind(endpoint)
                        stream.attachActorGateway(node)
                        SampleSupport.connectRawTcp(endpoint).use { client ->
                            SampleSupport.waitStreamConnected(monitor)
                            SampleSupport.sendRawTcp(client, "seed".toByteArray())
                            val sessionRid: RoutingId = Received().use { received ->
                                stream.recv(received, RecvFlags.NONE)
                                received.routingId.orElseThrow()
                            }
                            stream.bindActor(sessionRid, actorRef)
                                .timeout(Duration.ofSeconds(2))
                                .submitAsync().toCompletableFuture().join().forEach(Message::close)

                            Message.from("join-first").use { request ->
                                actor.join(spot).message(request).timeout(Duration.ofSeconds(2))
                                    .submit { result, messages ->
                                        replies.add(result.result())
                                        messages.forEach(Message::close)
                                    }
                            }
                            SampleSupport.waitUntil("actor join") { replies.isNotEmpty() }

                            Message.from("before").use { payload ->
                                stream.sendBoundActor(sessionRid, "single-player")
                                    .message(payload).submit()
                            }
                            actor.leave(spot).submitAsync().toCompletableFuture().join().forEach(Message::close)
                            Message.from("between").use { payload ->
                                stream.sendBoundActor(sessionRid, "single-player")
                                    .message(payload).submit()
                            }

                            Message.from("join-second").use { request ->
                                actor.join(spot).message(request).timeout(Duration.ofSeconds(2))
                                    .submit { result, messages ->
                                        replies.add(result.result())
                                        messages.forEach(Message::close)
                                    }
                            }
                            SampleSupport.waitUntil("queued actor payload") { payloads.size >= 2 }
                            check(payloads == listOf("before", "between")) {
                                "queued payloads were not preserved"
                            }
                            actor.leave(spot).submitAsync().toCompletableFuture().join().forEach(Message::close)
                            actor.close()
                        }
                        println("[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"")
                    }
                }
            }
        }
    }
}
