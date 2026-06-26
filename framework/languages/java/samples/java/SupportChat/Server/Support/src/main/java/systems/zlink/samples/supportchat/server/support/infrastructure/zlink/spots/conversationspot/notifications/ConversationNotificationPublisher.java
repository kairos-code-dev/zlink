package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.conversationspot.notifications;

import java.util.List;
import java.util.Map;
import java.util.function.Consumer;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationEvent;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.Roles;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class ConversationNotificationPublisher {
    public void publish(
        List<ConversationEvent> events,
        Map<String, SupportUserActor> actors) {
        for (ConversationEvent event : events) {
            publish(event, actors);
        }
    }

    public void publishJoinedAgentToCustomer(
        SupportUserActor customer,
        Messages.ConversationState state) {
        if (state.agentActorId() == null) {
            return;
        }
        customer.context().boundSession()
            .send(new Messages.ParticipantJoinedNotify(
                state.conversationId(),
                state.agentActorId(),
                Roles.Agent,
                state))
            .await();
    }

    private void publish(ConversationEvent event, Map<String, SupportUserActor> actors) {
        switch (event.kind()) {
            case PARTICIPANT_JOINED -> publishParticipantJoined(event, actors);
            case ASSIGNED -> publishAssigned(event, actors);
            case MESSAGE_APPENDED -> publishMessage(event, actors);
            case TYPING_CHANGED -> publishTyping(event, actors);
            case IDLE -> publishAll(actors, actor -> actor.context().boundSession()
                .send(new Messages.ConversationIdleNotify(
                    event.state().conversationId(),
                    event.state()))
                .await());
            case CLOSED -> publishAll(actors, actor -> actor.context().boundSession()
                .send(new Messages.ConversationClosedNotify(
                    event.state().conversationId(),
                    event.state()))
                .await());
        }
    }

    private void publishParticipantJoined(
        ConversationEvent event,
        Map<String, SupportUserActor> actors) {
        String customerActorId = event.state().customerActorId();
        SupportUserActor customer = actors.get(customerActorId);
        if (customer != null && !customer.actorId().equals(event.actorId())) {
            customer.context().boundSession()
                .send(new Messages.ParticipantJoinedNotify(
                    event.state().conversationId(),
                    event.actorId(),
                    event.role(),
                    event.state()))
                .await();
        }
    }

    private void publishAssigned(
        ConversationEvent event,
        Map<String, SupportUserActor> actors) {
        SupportUserActor agent = actors.get(event.actorId());
        if (agent == null) {
            return;
        }
        agent.context().boundSession()
            .send(new Messages.ConversationAssignedNotify(
                event.state().conversationId(),
                event.state()))
            .await();
    }

    private void publishMessage(
        ConversationEvent event,
        Map<String, SupportUserActor> actors) {
        Messages.ChatMessage message = event.message();
        for (SupportUserActor actor : actors.values()) {
            if (actor.actorId().equals(message.senderActorId())) {
                continue;
            }
            actor.context().boundSession()
                .send(new Messages.ChatMessageNotify(
                    event.state().conversationId(),
                    message,
                    event.state()))
                .await();
        }
    }

    private void publishTyping(
        ConversationEvent event,
        Map<String, SupportUserActor> actors) {
        for (SupportUserActor actor : actors.values()) {
            if (actor.actorId().equals(event.actorId())) {
                continue;
            }
            actor.context().boundSession()
                .send(new Messages.TypingChangedNotify(
                    event.state().conversationId(),
                    event.actorId(),
                    Boolean.TRUE.equals(event.isTyping()),
                    event.state()))
                .await();
        }
    }

    private void publishAll(
        Map<String, SupportUserActor> actors,
        Consumer<SupportUserActor> publish) {
        for (SupportUserActor actor : actors.values()) {
            publish.accept(actor);
        }
    }
}
