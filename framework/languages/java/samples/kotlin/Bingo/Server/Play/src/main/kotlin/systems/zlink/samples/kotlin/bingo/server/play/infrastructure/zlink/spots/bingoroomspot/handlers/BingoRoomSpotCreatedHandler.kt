package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot

class BingoRoomSpotCreatedHandler(
    private val json: ObjectMapper,
) {
    fun handle(
        spot: BingoRoomSpot,
        request: ZLinkMessage,
    ) {
        spot.applySettings(decodeSettings(request))
    }

    private fun decodeSettings(request: ZLinkMessage): BingoRoomSettings {
        if (request.isEmpty()) {
            return BingoRoomSettings.create(
                "two-player",
                0,
                SampleTimings.DrawPeriod.toMillis(),
            )
        }
        return request.decode(BingoRoomSettings::class.java)
    }
}
