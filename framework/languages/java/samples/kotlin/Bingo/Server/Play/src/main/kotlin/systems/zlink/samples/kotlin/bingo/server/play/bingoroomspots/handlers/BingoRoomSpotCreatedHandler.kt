package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import systems.zlink.contracts.messaging.Message
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSettings
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot

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
            return BingoRoomSettings.create("four-player", 0)
        }
        return json.readValue(request.toByteArray(), BingoRoomSettings::class.java)
    }
}
