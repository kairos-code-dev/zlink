package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.BingoRoomSpot

class BingoRoomDirectory(
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
) {
    private val gate = Mutex()
    private val actorAllocations = mutableMapOf<String, RoomAllocation>()
    private var currentRoomId: String? = null
    private var currentRoomSettings: BingoRoomSettings? = null
    private var reservedSeats: Int = 0
    private var roomSeq: Int = 0

    suspend fun allocate(
        actorId: String,
        mode: String,
    ): String {
        if (actorId.isBlank()) {
            throw IllegalStateException("actorId is required")
        }
        if (mode != "two-player") {
            throw IllegalStateException("Unsupported bingo mode. mode=$mode")
        }

        var settings: BingoRoomSettings? = null
        val roomId = gate.withLock {
            actorAllocations[actorId]?.also { allocation ->
                settings = allocation.settings
            }?.roomId ?: run {
                var nextSettings = BingoRoomSettings.create(mode, roomSeq + 1)
                if (
                    currentRoomId == null ||
                    currentRoomSettings == null ||
                    currentRoomSettings?.mode != nextSettings.mode ||
                    reservedSeats >= currentRoomSettings!!.requiredPlayers
                ) {
                    nextSettings = BingoRoomSettings.create(mode, ++roomSeq)
                    currentRoomId = "bingo-room-%03d".format(roomSeq)
                    currentRoomSettings = nextSettings
                    reservedSeats = 0
                }
                settings = nextSettings
                reservedSeats++
                currentRoomId!!.also {
                    actorAllocations[actorId] = RoomAllocation(it, nextSettings)
                }
            }
        }

        val settingsPart = Message.from(json.writeValueAsBytes(settings))
        return try {
            spots.getOrCreate(BingoRoomSpot::class.java, RoutingId.from(roomId), settingsPart)
                .await()
            roomId
        } finally {
            settingsPart.close()
        }
    }

    private data class RoomAllocation(
        val roomId: String,
        val settings: BingoRoomSettings,
    )
}
