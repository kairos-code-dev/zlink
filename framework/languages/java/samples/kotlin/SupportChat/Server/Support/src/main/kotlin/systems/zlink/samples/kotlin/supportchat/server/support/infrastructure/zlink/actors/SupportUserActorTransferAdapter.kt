package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors

import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkSuspendingActorTransferAdapter
import systems.zlink.framework.messaging.ZLinkMessage

class SupportUserActorTransferAdapter : ZLinkSuspendingActorTransferAdapter<SupportUserActor>() {
    override suspend fun transferOutSuspending(
        actor: SupportUserActor,
    ): ZLinkMessage = ZLinkMessage.of(
        TransferState(actor.displayName, actor.role, actor.participantId, actor.conversationId),
    )

    override suspend fun transferInSuspending(
        actorId: String,
        context: ZLinkActorContext,
        state: ZLinkMessage,
    ): SupportUserActor {
        val transferred = state.decode(TransferState::class.java)
        return SupportUserActor(actorId, context).also { actor ->
            actor.setIdentity(transferred.displayName, transferred.role, transferred.participantId)
            if (transferred.conversationId.isNotBlank()) {
                actor.joinConversation(transferred.conversationId)
            }
        }
    }

    data class TransferState(
        val displayName: String,
        val role: String,
        val participantId: String,
        val conversationId: String,
    )
}
