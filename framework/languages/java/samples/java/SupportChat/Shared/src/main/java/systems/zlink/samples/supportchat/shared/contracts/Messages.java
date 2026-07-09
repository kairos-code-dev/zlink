package systems.zlink.samples.supportchat.shared.contracts;

public final class Messages {
    private Messages() {
    }

    public record AuthenticateReq(String accessToken) {
    }

    public record AuthenticateRes(String actorId, String displayName, String role) {
    }

    public record AuthenticateUserReq(String accessToken) {
    }

    public record AuthenticateUserRes(
        boolean accepted,
        String actorId,
        String displayName,
        String role,
        String reason) {
    }

    public record OpenConversationApiReq(
        String customerActorId,
        String customerDisplayName,
        String subject) {
    }

    public record OpenConversationApiRes(String conversationId, String status) {
    }

    public record AllocateConversationReq(
        String customerActorId,
        String customerDisplayName,
        String subject) {
    }

    public record AllocateConversationRes(String conversationId, String status, ConversationState state) {
    }

    public record ActorRefWire(String nodeRid, String actorId, long generation) {
    }

    public record EnsureSupportUserActorReq(String actorId, String displayName, String role) {
    }

    public record EnsureSupportUserActorRes(String actorId, ActorRefWire actor) {
    }

    public record OpenConversationReq(String subject) {
    }

    public record OpenConversationRes(String conversationId, ConversationState state) {
    }

    public record SetAgentAvailableReq(boolean isAvailable) {
    }

    public record SetAgentAvailableRes(boolean isAvailable) {
    }

    public record JoinConversationReq(String conversationId) {
    }

    public record JoinConversationRes(ConversationState state) {
    }

    public record SendChatMessageReq(String conversationId, String text) {
    }

    public record SendChatMessageRes(ChatMessage message, ConversationState state) {
    }

    public record SetTypingReq(String conversationId, boolean isTyping) {
    }

    public record CloseConversationReq(String conversationId, String reason) {
    }

    public record CloseConversationRes(ConversationState state) {
    }

    public record ParticipantJoinedNotify(
        String conversationId,
        String actorId,
        String role,
        ConversationState state) {
    }

    public record ConversationAssignedNotify(String conversationId, ConversationState state) {
    }

    public record ChatMessageNotify(
        String conversationId,
        ChatMessage message,
        ConversationState state) {
    }

    public record TypingChangedNotify(
        String conversationId,
        String actorId,
        boolean isTyping,
        ConversationState state) {
    }

    public record ConversationIdleNotify(String conversationId, ConversationState state) {
    }

    public record ConversationClosedNotify(String conversationId, ConversationState state) {
    }

    public record ConversationState(
        String conversationId,
        String subject,
        String status,
        String customerActorId,
        String agentActorId,
        long lastMessageSeq,
        Long lastMessageAtUnixMs,
        Long idleDeadlineUnixMs) {
    }

    public record ChatMessage(
        String conversationId,
        long messageSeq,
        String senderActorId,
        String text,
        long sentAtUnixMs) {
    }

    public record JoinConversationSupportReq(String conversationId, String actorId, String displayName, String role) {
    }

    public record SendChatMessageSupportReq(String conversationId, String actorId, String text) {
    }

    public record SetTypingSupportReq(String conversationId, String actorId, boolean isTyping) {
    }

    public record CloseConversationSupportReq(String conversationId, String actorId, String reason) {
    }

    public record ServerAssertionRequest(String conversationId) {
    }

    public record ServerAssertionResponse(boolean passed, String reason) {
    }
}
