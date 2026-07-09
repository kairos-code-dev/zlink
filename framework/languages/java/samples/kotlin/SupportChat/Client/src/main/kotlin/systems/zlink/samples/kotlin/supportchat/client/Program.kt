package systems.zlink.samples.kotlin.supportchat.client

import java.net.URI
import java.time.Duration
import kotlinx.coroutines.future.await
import kotlinx.coroutines.runBlocking
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTimings
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode

fun main(args: Array<String>) = runBlocking {
    val streamEndpoint = readOption(args, "--stream-endpoint")
        ?: throw IllegalArgumentException("Missing --stream-endpoint.")
    val clients = List(6) { createClient(streamEndpoint) }
    try {
        SupportChatClientScenario().run(
            agent = clients[0],
            customer1 = clients[1],
            customer2 = clients[2],
            reconnectingAgent = clients[3],
            reconnectingCustomer = clients[4],
            waitingCustomer = clients[5],
        )
        println(SampleNames.ClientMarker)
    } finally {
        clients.forEach { client ->
            runCatching { client.close().submit().await() }
        }
    }
}

private fun createClient(streamEndpoint: String): ZLinkStreamConnector =
    ZLinkStreamConnectorFactory.create(
        ZLinkStreamConnectorOptions(
            URI.create(streamEndpoint),
            ZLinkStreamDispatchMode.AUTO,
            SampleTimings.RequestTimeout,
            2,
            SampleTimings.ConnectTimeout,
            64 * 1024,
            true,
            Duration.ofSeconds(1),
            SampleTimings.RequestTimeout.plusSeconds(5),
            true,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
        ),
    )

private fun readOption(args: Array<String>, name: String): String? {
    val index = args.indexOf(name)
    return if (index >= 0 && index + 1 < args.size) args[index + 1] else null
}
