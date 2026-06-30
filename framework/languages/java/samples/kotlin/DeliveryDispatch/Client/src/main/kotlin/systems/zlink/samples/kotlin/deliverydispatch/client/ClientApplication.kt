package systems.zlink.samples.kotlin.deliverydispatch.client

import java.net.URI
import java.time.Duration
import systems.zlink.framework.kotlin.ZLinkKotlinStreamConnector
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.kotlin
import systems.zlink.httpclient.kotlin.zlinkHttpClient
import systems.zlink.samples.kotlin.deliverydispatch.client.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.client.configuration.SampleTopology
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode

class ClientApplication {
    companion object {
        suspend fun run(args: Array<String> = emptyArray()) {
            zlinkHttpClient(SampleTopology.ApiHttpUrl).use { api ->
                val customer = createConnector()
                try {
                    DeliveryDispatchClientScenario(api).run(customer)
                } finally {
                    customer.close().await()
                }
            }
            println("deliverydispatch=completed")
        }

        private fun createConnector(): ZLinkKotlinStreamConnector =
            ZLinkStreamConnectorFactory.create(
                ZLinkStreamConnectorOptions(
                    URI.create(SampleTopology.SessionStreamEndpoint),
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
                ),
            ).kotlin()
    }
}
