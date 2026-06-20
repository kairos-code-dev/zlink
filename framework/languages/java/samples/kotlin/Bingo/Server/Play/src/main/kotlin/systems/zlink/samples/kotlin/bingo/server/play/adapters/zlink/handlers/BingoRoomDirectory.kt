package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.spots.ZLinkSpotCreateState
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.BingoRoomSpot

class BingoRoomDirectory(
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
    private val matchQueue: BingoMatchQueue,
) {
    private var roomSeq: Int = 0

    suspend fun allocate(
        actorId: String,
        mode: String,
        preferredOwnerNodeRid: String,
    ): BingoMatchReservation {
        if (actorId.isBlank()) {
            throw IllegalStateException("actorId is required")
        }
        if (mode != "two-player") {
            throw IllegalStateException("Unsupported bingo mode. mode=$mode")
        }

        val settings = BingoRoomSettings.create(mode, nextRoomSeq())
        val reservation = matchQueue.reserve(
            mode,
            actorId,
            preferredOwnerNodeRid,
            "${settings.mode}-room-$roomSeq",
            settings.requiredPlayers,
        )
        if (reservation.ownerPlayNodeRid != SampleTopology.selectedPlayNodeRid()) {
            println(
                "bingo room allocation: remote owner. room=${reservation.roomId}, " +
                    "owner=${reservation.ownerPlayNodeRid}, local=${SampleTopology.selectedPlayNodeRid()}",
            )
            return reservation
        }

        val settingsPart = Message.from(json.writeValueAsBytes(settings))
        return try {
            val created = spots.getOrCreate(BingoRoomSpot::class.java, RoutingId.from(reservation.roomId), settingsPart)
                .await()
            check(created.state() != ZLinkSpotCreateState.REJECTED) {
                "Bingo room creation was rejected. room=${reservation.roomId}"
            }
            println(
                "bingo room allocation: local owner. room=${reservation.roomId}, " +
                    "owner=${reservation.ownerPlayNodeRid}, local=${SampleTopology.selectedPlayNodeRid()}, " +
                    "state=${created.state()}",
            )
            reservation
        } finally {
            settingsPart.close()
        }
    }

    @Synchronized
    private fun nextRoomSeq(): Int {
        roomSeq += 1
        return roomSeq
    }
}
