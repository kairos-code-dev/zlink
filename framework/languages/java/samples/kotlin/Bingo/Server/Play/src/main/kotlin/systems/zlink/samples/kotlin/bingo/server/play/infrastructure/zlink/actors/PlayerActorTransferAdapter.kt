package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors

import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkSuspendingActorTransferAdapter
import systems.zlink.framework.messaging.ZLinkMessage

class PlayerActorTransferAdapter : ZLinkSuspendingActorTransferAdapter<PlayerActor>() {
    override suspend fun transferOutSuspending(
        actor: PlayerActor,
    ): ZLinkMessage = ZLinkMessage.of(
        TransferState(
            actor.displayName,
            actor.roomId,
            actor.destroyAfterEntrySpotJoin,
            actor.disconnected,
        ),
    )

    override suspend fun transferInSuspending(
        actorId: String,
        context: ZLinkActorContext,
        state: ZLinkMessage,
    ): PlayerActor {
        val transferred = state.decode(TransferState::class.java)
        return PlayerActor(actorId, context).also { actor ->
            actor.setDisplayName(transferred.displayName)
            if (transferred.roomId.isNotBlank()) actor.joinRoom(transferred.roomId)
            if (transferred.destroyAfterEntrySpotJoin) actor.markForDestroyAfterRoomLeave()
            if (transferred.disconnected) actor.markDisconnected()
        }
    }

    data class TransferState(
        val displayName: String,
        val roomId: String,
        val destroyAfterEntrySpotJoin: Boolean,
        val disconnected: Boolean,
    )
}
