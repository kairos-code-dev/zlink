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
        createParts: List<Message>,
    ) {
        spot.applySettings(decodeSettings(createParts))
    }

    private fun decodeSettings(createParts: List<Message>): BingoRoomSettings {
        if (createParts.isEmpty()) {
            return BingoRoomSettings.create("four-player", 0)
        }
        check(createParts.size == 1) {
            "Bingo room expects exactly one create payload part, but received ${createParts.size}."
        }
        return json.readValue(createParts.first().toByteArray(), BingoRoomSettings::class.java)
    }
}
