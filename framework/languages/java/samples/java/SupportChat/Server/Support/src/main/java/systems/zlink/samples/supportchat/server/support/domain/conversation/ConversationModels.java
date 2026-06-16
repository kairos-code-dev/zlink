package systems.zlink.samples.supportchat.server.support.domain.conversation;

import java.util.List;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class ConversationModels {
    private ConversationModels() {
    }

    public static final class Statuses {
        public static final String WaitingForAgent =
            systems.zlink.samples.supportchat.server.configuration.ConversationStatuses.WaitingForAgent;
        public static final String Active =
            systems.zlink.samples.supportchat.server.configuration.ConversationStatuses.Active;
        public static final String WaitingForClose =
            systems.zlink.samples.supportchat.server.configuration.ConversationStatuses.WaitingForClose;
        public static final String Closed =
            systems.zlink.samples.supportchat.server.configuration.ConversationStatuses.Closed;

        private Statuses() {
        }
    }

    public static final class Roles {
        public static final String Customer =
            systems.zlink.samples.supportchat.server.configuration.SupportChatRoles.Customer;
        public static final String Agent =
            systems.zlink.samples.supportchat.server.configuration.SupportChatRoles.Agent;

        private Roles() {
        }
    }

    public record ConversationPolicy(
        long idleTimeoutMillis,
        long closeGraceTimeoutMillis,
        int maxMessageLength) {
        public static ConversationPolicy sample() {
            return new ConversationPolicy(3000, 2000, 500);
        }
    }

    public record ConversationParticipant(
        String actorId,
        String role,
        String displayName,
        long joinedAtUnixMs,
        boolean isTyping) {
        public ConversationParticipant withTyping(boolean typing) {
            return new ConversationParticipant(actorId, role, displayName, joinedAtUnixMs, typing);
        }
    }

    public record ConversationMessage(
        String conversationId,
        long messageSeq,
        String senderActorId,
        String text,
        long sentAtUnixMs) {
    }

    public enum EventKind {
        PARTICIPANT_JOINED,
        ASSIGNED,
        MESSAGE_APPENDED,
        TYPING_CHANGED,
        IDLE,
        CLOSED
    }

    public record ConversationEvent(
        EventKind kind,
        Messages.ConversationState state,
        String actorId,
        String role,
        Messages.ChatMessage message,
        Boolean isTyping) {
        public static ConversationEvent of(EventKind kind, Messages.ConversationState state) {
            return new ConversationEvent(kind, state, null, null, null, null);
        }

        public static ConversationEvent actor(
            EventKind kind,
            Messages.ConversationState state,
            String actorId) {
            return new ConversationEvent(kind, state, actorId, null, null, null);
        }

        public static ConversationEvent participant(
            EventKind kind,
            Messages.ConversationState state,
            String actorId,
            String role) {
            return new ConversationEvent(kind, state, actorId, role, null, null);
        }

        public static ConversationEvent message(
            Messages.ConversationState state,
            String actorId,
            Messages.ChatMessage message) {
            return new ConversationEvent(EventKind.MESSAGE_APPENDED, state, actorId, null, message, null);
        }

        public static ConversationEvent typing(
            Messages.ConversationState state,
            String actorId,
            String role,
            boolean isTyping) {
            return new ConversationEvent(EventKind.TYPING_CHANGED, state, actorId, role, null, isTyping);
        }
    }

    public record ConversationChange(
        Messages.ConversationState state,
        List<ConversationEvent> events) {
    }

    public record ConversationCreateRequest(
        String customerActorId,
        String customerDisplayName,
        String subject,
        long createdAtUnixMs) {
    }
}
