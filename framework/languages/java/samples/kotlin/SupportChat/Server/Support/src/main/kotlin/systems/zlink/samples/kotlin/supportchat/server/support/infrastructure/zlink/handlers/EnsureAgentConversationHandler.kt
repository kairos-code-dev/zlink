package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.handlers

import kotlinx.coroutines.future.await
import org.slf4j.LoggerFactory
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActorClient
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.actors.ActorRef
import systems.zlink.framework.actors.ActorRefSnapshot
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.supportchat.server.configuration.SupportChatRoles
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportActorDirectory
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureAgentConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureAgentConversationRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureSupportUserActorReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.JoinConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.JoinConversationRes

@ZLinkHandlerGroup(SampleNames.SupportChannel)
class EnsureAgentConversationHandler(
    private val actorClient: ZLinkActorClient,
    private val actors: ZLinkActorManager,
    private val directory: SupportActorDirectory,
) : ZLinkSuspendingRequestHandler<EnsureAgentConversationReq, EnsureAgentConversationRes> {
    override suspend fun handle(
        request: EnsureAgentConversationReq,
        context: ZLinkRequestContext,
    ): EnsureAgentConversationRes {
        val conversationActorId = "${request.rosterActorId}@${request.conversationId}"
        val existingActorRef = actors.find(conversationActorId).await().orElse(null)
        val zlinkActorRef = existingActorRef
            ?: actors.getOrCreate(
                conversationActorId,
                SampleNames.SupportActorType,
                EnsureSupportUserActorReq(
                    actorId = conversationActorId,
                    displayName = request.displayName,
                    role = SupportChatRoles.Agent,
                    participantId = request.rosterActorId,
                ),
            ).await()
        val actorRef = zlinkActorRef

        val joined = if (existingActorRef == null) {
            val conversationActor = directory.get(conversationActorId)
            val initialJoin = conversationActor.actor.context()
                .joinSpot(
                    RoutingId.from(request.conversationId),
                    JoinConversationReq(
                        request.rosterActorId,
                        SupportChatRoles.Agent,
                        request.displayName,
                    ),
                )
                .submit(JoinConversationRes::class.java)
                .await()
            val target = (initialJoin as? systems.zlink.framework.actors.ZLinkActorJoinResult.Accepted)
                ?.actor()
                ?: throw IllegalStateException("Agent conversation join was rejected.")
            actorClient
                .requestToActor(target.actorId, JoinConversationReq())
                .timeout(SampleTimings.RequestTimeout)
                .submit(JoinConversationRes::class.java)
                .await()
        } else {
            actorClient
                .requestToActor(actorRef.actorId, JoinConversationReq())
                .timeout(SampleTimings.RequestTimeout)
                .submit(JoinConversationRes::class.java)
                .await()
        }

        logger.info(
            "support agent conversation: joined. conversation={}, roster={}",
            request.conversationId,
            request.rosterActorId,
        )
        return EnsureAgentConversationRes(ActorRefSnapshot.from(actorRef), joined.state)
    }

    private companion object {
        private val logger = LoggerFactory.getLogger(EnsureAgentConversationHandler::class.java)
    }
}
