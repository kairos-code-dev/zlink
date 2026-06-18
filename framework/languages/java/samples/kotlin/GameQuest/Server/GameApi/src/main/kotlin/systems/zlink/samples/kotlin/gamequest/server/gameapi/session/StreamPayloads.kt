package systems.zlink.samples.kotlin.gamequest.server.gameapi.session

import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.streams.ZLinkStreamCodec as FrameworkStreamCodec
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.stream.connector.ZLinkStreamCodec
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload
import systems.zlink.stream.connector.ZLinkStreamJson

/** Decodes inbound JSON stream packets into typed session requests. */
object StreamPayloads {
    fun <T> decode(header: ZLinkStreamHeader, payload: Message, type: Class<T>): T =
        ZLinkStreamJson.decode(
            ZLinkStreamEncodedPayload(
                header.packetName(),
                payload,
                header.metadata(),
                connectorCodec(header),
            ),
            type,
        )

    private fun connectorCodec(header: ZLinkStreamHeader): ZLinkStreamCodec =
        when (header.codec()) {
            FrameworkStreamCodec.RAW -> ZLinkStreamCodec.RAW
            FrameworkStreamCodec.JSON -> ZLinkStreamCodec.JSON
            FrameworkStreamCodec.MESSAGE_PACK -> ZLinkStreamCodec.MESSAGE_PACK
            FrameworkStreamCodec.PROTOBUF -> ZLinkStreamCodec.PROTOBUF
        }
}
