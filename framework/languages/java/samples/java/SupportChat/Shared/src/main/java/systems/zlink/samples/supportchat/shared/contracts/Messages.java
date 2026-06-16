package systems.zlink.samples.supportchat.shared.contracts;

import systems.zlink.framework.handlers.ZLinkPacket;

public final class Messages {
    private Messages() {
    }

    public record AuthenticateReq(String accessToken) {
    }

    public record AuthenticateRes(String actorId, String displayName, String role) {
    }

    @ZLinkPacket("AuthenticateUser")
    public record AuthenticateUserReq(String accessToken) {
    }

    public record AuthenticateUserRes(
        boolean accepted,
        String actorId,
        String displayName,
        String role,
        String reason) {
    }

    @ZLinkPacket("EnsureSupportUserActor")
    public record EnsureSupportUserActorReq(String actorId, String displayName, String role) {
    }

    public record ActorRefSnapshot(byte[] nodeRid, String actorId, long generation) {
    }

    public record EnsureSupportUserActorRes(ActorRefSnapshot actor) {
    }

    @ZLinkPacket("OpenConversationApiReq")
    public record OpenConversationApiReq(
        String customerActorId,
        String customerDisplayName,
        String subject) {
    }

    public record OpenConversationApiRes(String conversationId, String status) {
    }

    @ZLinkPacket("AllocateConversationReq")
    public record AllocateConversationReq(
        String customerActorId,
        String customerDisplayName,
        String subject) {
    }

    public record AllocateConversationRes(String conversationId, String status) {
    }

    @ZLinkPacket("AssignAgentReq")
    public record AssignAgentReq(String conversationId, String requestedAgentActorId) {
    }

    public record AssignAgentRes(String conversationId, String status, String agentActorId) {
    }

    @ZLinkPacket("OpenConversationReq")
    public record OpenConversationReq(String subject) {
    }

    public record OpenConversationRes(String conversationId, ConversationState state) {
    }

    @ZLinkPacket("SetAgentAvailableReq")
    public record SetAgentAvailableReq(boolean isAvailable) {
    }

    public record SetAgentAvailableRes(boolean isAvailable) {
    }

    @ZLinkPacket("JoinConversationReq")
    public record JoinConversationReq(String conversationId) {
    }

    public record JoinConversationRes(ConversationState state) {
    }

    @ZLinkPacket("SendChatMessageReq")
    public record SendChatMessageReq(String conversationId, String text) {
    }

    public record SendChatMessageRes(ChatMessage message, ConversationState state) {
    }

    @ZLinkPacket("SetTypingReq")
    public record SetTypingReq(String conversationId, boolean isTyping) {
    }

    public record SetTypingRes(ConversationState state) {
    }

    @ZLinkPacket("CloseConversationReq")
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
}
