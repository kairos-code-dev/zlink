package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.notifications

import kotlinx.coroutines.future.await
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.ConversationEvent
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.ConversationEventKind
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.Roles
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ChatMessageNotify
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ConversationAssignedNotify
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ConversationClosedNotify
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ConversationIdleNotify
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ConversationState
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ParticipantJoinedNotify
import systems.zlink.samples.kotlin.supportchat.shared.contracts.TypingChangedNotify

class ConversationNotificationPublisher {
    suspend fun publish(
        events: List<ConversationEvent>,
        actors: Map<String, SupportUserActor>,
    ) {
        for (event in events) {
            publish(event, actors)
        }
    }

    suspend fun publishJoinedAgentToCustomer(
        customer: SupportUserActor,
        state: ConversationState,
    ) {
        val agentActorId = state.agentActorId ?: return
        customer.context().boundSession()
            .send(ParticipantJoinedNotify(state.conversationId, agentActorId, Roles.Agent, state))
            .submit()
            .await()
    }

    private suspend fun publish(
        event: ConversationEvent,
        actors: Map<String, SupportUserActor>,
    ) {
        when (event.kind) {
            ConversationEventKind.PARTICIPANT_JOINED -> publishParticipantJoined(event, actors)
            ConversationEventKind.ASSIGNED -> publishAssigned(event, actors)
            ConversationEventKind.MESSAGE_APPENDED -> publishMessage(event, actors)
            ConversationEventKind.TYPING_CHANGED -> publishTyping(event, actors)
            ConversationEventKind.IDLE ->
                for (actor in actors.values) {
                    actor.context().boundSession()
                        .send(ConversationIdleNotify(event.state.conversationId, event.state))
                        .submit()
                        .await()
                }
            ConversationEventKind.CLOSED ->
                for (actor in actors.values) {
                    actor.context().boundSession()
                        .send(ConversationClosedNotify(event.state.conversationId, event.state))
                        .submit()
                        .await()
                }
        }
    }

    private suspend fun publishParticipantJoined(
        event: ConversationEvent,
        actors: Map<String, SupportUserActor>,
    ) {
        val customer = actors[event.state.customerActorId]
        if (customer != null && customer.actorId() != event.actorId) {
            customer.context().boundSession()
                .send(
                    ParticipantJoinedNotify(
                        event.state.conversationId,
                        event.actorId!!,
                        event.role!!,
                        event.state,
                    ),
                )
                .submit()
                .await()
        }
    }

    private suspend fun publishAssigned(
        event: ConversationEvent,
        actors: Map<String, SupportUserActor>,
    ) {
        val agent = actors[event.actorId] ?: return
        agent.context().boundSession()
            .send(ConversationAssignedNotify(event.state.conversationId, event.state))
            .submit()
            .await()
    }

    private suspend fun publishMessage(
        event: ConversationEvent,
        actors: Map<String, SupportUserActor>,
    ) {
        val message = event.message!!
        for (actor in actors.values) {
            if (actor.actorId() == message.senderActorId) {
                continue
            }
            actor.context().boundSession()
                .send(ChatMessageNotify(event.state.conversationId, message, event.state))
                .submit()
                .await()
        }
    }

    private suspend fun publishTyping(
        event: ConversationEvent,
        actors: Map<String, SupportUserActor>,
    ) {
        for (actor in actors.values) {
            if (actor.actorId() == event.actorId) {
                continue
            }
            actor.context().boundSession()
                .send(
                    TypingChangedNotify(
                        event.state.conversationId,
                        event.actorId!!,
                        event.isTyping == true,
                        event.state,
                    ),
                )
                .submit()
                .await()
        }
    }
}
