package systems.zlink.e2e.kotlin.spotservice.client.support

import java.net.URI
import java.time.Duration
import java.util.concurrent.CompletionStage
import systems.zlink.stream.connector.ZLinkStreamCompression
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode

internal val REQUEST_TIMEOUT: Duration = Duration.ofSeconds(5)

internal fun <T> awaitUnchecked(
    connector: ZLinkStreamConnector,
    stage: CompletionStage<T>,
) {
    try {
        connector.await(stage)
    } catch (error: Exception) {
        throw RuntimeException(error)
    }
}

internal fun createStreamConnector(endpoint: String): ZLinkStreamConnector =
    createStreamConnector(endpoint, ZLinkStreamDispatchMode.AUTO, 2)

internal fun createStreamConnector(
    endpoint: String,
    dispatchMode: ZLinkStreamDispatchMode,
    maxReceivedMessages: Int,
): ZLinkStreamConnector =
    ZLinkStreamConnectorFactory.create(
        ZLinkStreamConnectorOptions(
            URI.create(endpoint),
            dispatchMode,
            REQUEST_TIMEOUT,
            2,
            Duration.ofSeconds(5),
            64 * 1024,
            64 * 1024,
            maxReceivedMessages,
            true,
            Duration.ofSeconds(1),
            Duration.ofSeconds(5),
            false,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            false,
            ZLinkStreamCompression.LZ4,
            null,
            null,
        ),
    )
