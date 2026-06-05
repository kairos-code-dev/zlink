package systems.zlink.samples.kotlin.bingo.server.play.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSettings
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot

class BingoRoomDirectory(
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
) {
    private val gate = Any()
    private val actorRooms = mutableMapOf<String, String>()
    private var currentRoomId: String? = null
    private var currentRoomSettings: BingoRoomSettings? = null
    private var reservedSeats: Int = 0
    private var roomSeq: Int = 0

    fun allocateAsync(
        actorId: String,
        mode: String,
    ): CompletionStage<String> {
        if (actorId.isBlank()) {
            return CompletableFuture.failedFuture(
                IllegalStateException("actorId is required"),
            )
        }
        if (mode != "four-player") {
            return CompletableFuture.failedFuture(
                IllegalStateException("Unsupported bingo mode. mode=$mode"),
            )
        }

        var settings: BingoRoomSettings? = null
        val roomId = synchronized(gate) {
            actorRooms[actorId]?.also {
                settings = currentRoomSettings
            } ?: run {
                var nextSettings = BingoRoomSettings.create(mode, roomSeq + 1)
                if (
                    currentRoomId == null ||
                    currentRoomSettings == null ||
                    currentRoomSettings?.mode != nextSettings.mode ||
                    reservedSeats >= currentRoomSettings!!.requiredPlayers
                ) {
                    nextSettings = BingoRoomSettings.create(mode, ++roomSeq)
                    currentRoomId = RoutingId.from("bingo-room-%03d".format(roomSeq)).toHex()
                    currentRoomSettings = nextSettings
                    reservedSeats = 0
                }
                settings = nextSettings
                reservedSeats++
                currentRoomId!!.also { actorRooms[actorId] = it }
            }
        }

        val settingsPart = Message.from(json.writeValueAsBytes(settings))
        return spots.getOrCreateAsync(BingoRoomSpot::class.java, RoutingId.fromHex(roomId), listOf(settingsPart))
            .thenApply { roomId }
            .whenComplete { _, _ -> settingsPart.close() }
    }
}
