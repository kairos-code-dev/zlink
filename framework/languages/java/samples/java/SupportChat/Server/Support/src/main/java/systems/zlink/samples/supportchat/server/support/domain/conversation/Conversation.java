package systems.zlink.samples.supportchat.server.support.domain.conversation;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationChange;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationEvent;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationMessage;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationParticipant;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationPolicy;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.EventKind;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.Roles;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.Statuses;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class Conversation {
    private final String conversationId;
    private final String subject;
    private final String customerActorId;
    private final ConversationPolicy policy;
    private final Map<String, ConversationParticipant> participants = new LinkedHashMap<>();
    private final List<ConversationMessage> messages = new ArrayList<>();
    private String status;
    private String agentActorId;
    private long lastMessageSeq;
    private Long lastMessageAtUnixMs;
    private Long idleDeadlineUnixMs;

    public Conversation(
        String conversationId,
        String subject,
        String customerActorId,
        String customerDisplayName,
        long createdAtUnixMs,
        ConversationPolicy policy) {
        if (subject == null || subject.isBlank()) {
            throw new IllegalStateException("Conversation subject is required.");
        }
        this.conversationId = conversationId;
        this.subject = subject;
        this.customerActorId = customerActorId;
        this.status = Statuses.WaitingForAgent;
        this.policy = policy == null ? ConversationPolicy.sample() : policy;
        this.participants.put(customerActorId, new ConversationParticipant(
            customerActorId,
            Roles.Customer,
            customerDisplayName,
            createdAtUnixMs,
            false));
    }

    public String conversationId() {
        return conversationId;
    }

    public String customerActorId() {
        return customerActorId;
    }

    public Messages.ConversationState snapshot() {
        return new Messages.ConversationState(
            conversationId,
            subject,
            status,
            customerActorId,
            agentActorId,
            lastMessageSeq,
            lastMessageAtUnixMs,
            idleDeadlineUnixMs);
    }

    public ConversationChange joinAgent(
        String agentActorId,
        String agentDisplayName,
        long joinedAtUnixMs) {
        ensureNotClosed("join an agent");
        if (this.agentActorId != null && !this.agentActorId.equals(agentActorId)) {
            throw new IllegalStateException("Conversation already has an assigned agent.");
        }
        this.agentActorId = agentActorId;
        this.status = Statuses.Active;
        participants.put(agentActorId, new ConversationParticipant(
            agentActorId,
            Roles.Agent,
            agentDisplayName,
            joinedAtUnixMs,
            false));
        Messages.ConversationState state = snapshot();
        List<ConversationEvent> events = List.of(
            ConversationEvent.participant(EventKind.PARTICIPANT_JOINED, state, agentActorId, Roles.Agent),
            ConversationEvent.participant(EventKind.ASSIGNED, state, agentActorId, Roles.Agent));
        return new ConversationChange(state, events);
    }

    public ConversationChange sendMessage(
        String senderActorId,
        String text,
        long sentAtUnixMs) {
        ensureParticipant(senderActorId);
        if (status.equals(Statuses.Closed)) {
            throw new IllegalStateException("Closed conversation cannot accept messages.");
        }
        if (status.equals(Statuses.WaitingForAgent)) {
            throw new IllegalStateException("Conversation is waiting for an agent.");
        }
        if (text == null || text.isBlank()) {
            throw new IllegalStateException("Message text is required.");
        }
        if (text.length() > policy.maxMessageLength()) {
            throw new IllegalStateException("Message text is too long.");
        }
        status = Statuses.Active;
        lastMessageSeq += 1;
        lastMessageAtUnixMs = sentAtUnixMs;
        idleDeadlineUnixMs = sentAtUnixMs + policy.idleTimeoutMillis();
        ConversationMessage message = new ConversationMessage(
            conversationId,
            lastMessageSeq,
            senderActorId,
            text,
            sentAtUnixMs);
        messages.add(message);
        Messages.ConversationState state = snapshot();
        return new ConversationChange(
            state,
            List.of(ConversationEvent.message(state, senderActorId, toContract(message))));
    }

    public ConversationChange setTyping(String actorId, boolean isTyping) {
        ConversationParticipant participant = ensureParticipant(actorId);
        ensureNotClosed("change typing state");
        participants.put(actorId, participant.withTyping(isTyping));
        Messages.ConversationState state = snapshot();
        return new ConversationChange(
            state,
            List.of(ConversationEvent.typing(state, actorId, participant.role(), isTyping)));
    }

    public ConversationChange markIdle(long nowUnixMs) {
        if (!status.equals(Statuses.Active) || idleDeadlineUnixMs == null || nowUnixMs < idleDeadlineUnixMs) {
            return new ConversationChange(snapshot(), List.of());
        }
        status = Statuses.WaitingForClose;
        Messages.ConversationState state = snapshot();
        return new ConversationChange(
            state,
            List.of(ConversationEvent.of(EventKind.IDLE, state)));
    }

    public ConversationChange close(String actorId, String reason) {
        ensureParticipant(actorId);
        if (status.equals(Statuses.Closed)) {
            throw new IllegalStateException("Conversation is already closed.");
        }
        status = Statuses.Closed;
        Messages.ConversationState state = snapshot();
        return new ConversationChange(
            state,
            List.of(ConversationEvent.actor(EventKind.CLOSED, state, actorId)));
    }

    private ConversationParticipant ensureParticipant(String actorId) {
        ConversationParticipant participant = participants.get(actorId);
        if (participant == null) {
            throw new IllegalStateException("Actor is not a conversation participant.");
        }
        return participant;
    }

    private void ensureNotClosed(String action) {
        if (status.equals(Statuses.Closed)) {
            throw new IllegalStateException("Closed conversation cannot " + action + ".");
        }
    }

    private static Messages.ChatMessage toContract(ConversationMessage message) {
        return new Messages.ChatMessage(
            message.conversationId(),
            message.messageSeq(),
            message.senderActorId(),
            message.text(),
            message.sentAtUnixMs());
    }
}
