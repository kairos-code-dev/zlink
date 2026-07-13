package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.actors

import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkSuspendingActorTransferAdapter
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerInfo

class PlayActorTransferAdapter : ZLinkSuspendingActorTransferAdapter<PlayActor>() {
    override suspend fun transferOutSuspending(
        actor: PlayActor,
    ): ZLinkMessage = ZLinkMessage.of(
        TransferState(
            actor.joinedRoomIdOrNull(),
            actor.playerOrNull(),
            actor.destroyAfterEntrySpotJoin,
            actor.disconnected,
        ),
    )

    override suspend fun transferInSuspending(
        actorId: String,
        context: ZLinkActorContext,
        state: ZLinkMessage,
    ): PlayActor {
        val transferred = state.decode(TransferState::class.java)
        return PlayActor(actorId, context).also { actor ->
            transferred.player?.let(actor::applyPlayer)
            transferred.roomId?.takeIf(String::isNotBlank)?.let(actor::joinGame)
            if (transferred.destroyAfterEntrySpotJoin) actor.markForDestroyAfterRoomLeave()
            if (transferred.disconnected) actor.markDisconnected()
        }
    }

    data class TransferState(
        val roomId: String?,
        val player: PlayerInfo?,
        val destroyAfterEntrySpotJoin: Boolean,
        val disconnected: Boolean,
    )
}
