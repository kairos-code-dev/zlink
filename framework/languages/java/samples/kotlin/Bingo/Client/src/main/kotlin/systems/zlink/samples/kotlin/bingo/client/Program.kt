package systems.zlink.samples.kotlin.bingo.client

import java.net.URI
import java.time.Duration
import systems.zlink.framework.kotlin.ZLinkKotlinStreamConnector
import systems.zlink.framework.kotlin.kotlin
import systems.zlink.samples.kotlin.bingo.client.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.client.configuration.SampleTopology
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.protobuf.ZLinkStreamProtobuf

suspend fun main() {
    val client1 = createClient()
    val client2 = createClient()
    try {
        BingoClientScenario().run(client1, client2)
    } finally {
        client1.close().await()
        client2.close().await()
    }
    println("Bingo client self-check passed")
}

private fun createClient(): ZLinkKotlinStreamConnector =
    ZLinkStreamConnectorFactory.create(
        ZLinkStreamConnectorOptions(
            URI.create(SampleTopology.StreamEndpoint),
            ZLinkStreamDispatchMode.AUTO,
            SampleTimings.RequestTimeout,
            2,
            SampleTimings.ConnectTimeout,
            64 * 1024,
            false,
            Duration.ofSeconds(1),
            SampleTimings.RequestTimeout.plusSeconds(5),
            true,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            ZLinkStreamProtobuf.codec(),
        ),
    ).kotlin()
