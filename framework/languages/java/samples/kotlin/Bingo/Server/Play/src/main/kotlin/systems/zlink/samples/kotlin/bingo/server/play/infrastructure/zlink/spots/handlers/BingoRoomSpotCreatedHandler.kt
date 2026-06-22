package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.contracts.messaging.Message
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.BingoRoomSpot

class BingoRoomSpotCreatedHandler(
    private val json: ObjectMapper,
) {
    fun handle(
        spot: BingoRoomSpot,
        request: Message,
    ) {
        spot.applySettings(decodeSettings(request))
    }

    private fun decodeSettings(request: Message): BingoRoomSettings {
        if (request.isEmpty()) {
            return BingoRoomSettings.create(
                "two-player",
                0,
                SampleTimings.DrawPeriod.toMillis(),
            )
        }
        return json.readValue(request.toByteArray(), BingoRoomSettings::class.java)
    }
}
