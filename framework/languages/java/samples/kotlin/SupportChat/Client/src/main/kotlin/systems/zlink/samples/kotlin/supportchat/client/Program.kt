package systems.zlink.samples.kotlin.supportchat.client

import java.net.URI
import java.time.Duration
import systems.zlink.framework.kotlin.ZLinkKotlinStreamConnector
import systems.zlink.framework.kotlin.kotlin
import systems.zlink.samples.kotlin.supportchat.client.configuration.SampleTimings
import systems.zlink.samples.kotlin.supportchat.client.configuration.SampleTopology
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec

suspend fun main() {
    val customer = createClient()
    val agent = createClient()
    val reconnectingCustomer = createClient()
    val waitingCustomer = createClient()
    val closingCustomer = createClient()
    val closingAgent = createClient()
    try {
        SupportChatClientScenario().run(
            customer,
            agent,
            reconnectingCustomer,
            waitingCustomer,
            closingCustomer,
            closingAgent,
        )
    } finally {
        customer.close().await()
        agent.close().await()
        reconnectingCustomer.close().await()
        waitingCustomer.close().await()
        closingCustomer.close().await()
        closingAgent.close().await()
    }
    println("supportchat=completed")
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
            ZLinkProtobufCodec.defaultCodec(),
        ),
    ).kotlin()
