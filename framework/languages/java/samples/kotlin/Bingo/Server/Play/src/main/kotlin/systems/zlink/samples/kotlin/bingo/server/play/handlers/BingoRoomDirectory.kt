package systems.zlink.samples.kotlin.bingo.server.play.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot

class BingoRoomDirectory(
    private val spots: ZLinkSpotManager,
) {
    private val gate = Any()
    private val actorRooms = mutableMapOf<String, String>()
    private var currentRoomId: String? = null
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

        val roomId = synchronized(gate) {
            actorRooms[actorId] ?: run {
                if (currentRoomId == null || reservedSeats >= 4) {
                    roomSeq++
                    currentRoomId = RoutingId.from("bingo-room-%03d".format(roomSeq)).toHex()
                    reservedSeats = 0
                }
                reservedSeats++
                currentRoomId!!.also { actorRooms[actorId] = it }
            }
        }

        return spots.getOrCreateAsync(BingoRoomSpot::class.java, RoutingId.fromHex(roomId))
            .thenApply { roomId }
    }
}
