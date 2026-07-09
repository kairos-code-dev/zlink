package systems.zlink.samples.supportchat.server.support.domain;

import java.time.Instant;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class ConversationStore {
    private final Map<String, ConversationRecord> conversations = new LinkedHashMap<>();
    private final Set<String> availableAgents = new HashSet<>();
    private int nextConversation = 1;

    public synchronized Messages.SetAgentAvailableRes setAgentAvailable(boolean available) {
        if (available) {
            availableAgents.add("agent-1");
        } else {
            availableAgents.remove("agent-1");
        }
        return new Messages.SetAgentAvailableRes(availableAgents.contains("agent-1"));
    }

    public synchronized Messages.AllocateConversationRes allocate(Messages.AllocateConversationReq request) {
        String conversationId = "conv-" + nextConversation++;
        String agent = availableAgents.isEmpty() ? null : availableAgents.iterator().next();
        ConversationRecord record = new ConversationRecord(
            conversationId,
            request.subject(),
            "Open",
            request.customerActorId(),
            agent);
        conversations.put(conversationId, record);
        return new Messages.AllocateConversationRes(conversationId, record.status, state(record));
    }

    public synchronized Messages.JoinConversationRes join(Messages.JoinConversationSupportReq request) {
        ConversationRecord record = require(request.conversationId());
        if ("agent".equals(request.role())) {
            record.agentActorId = request.actorId();
        }
        return new Messages.JoinConversationRes(state(record));
    }

    public synchronized Messages.SendChatMessageRes append(Messages.SendChatMessageSupportReq request) {
        ConversationRecord record = require(request.conversationId());
        long seq = ++record.lastMessageSeq;
        long now = Instant.now().toEpochMilli();
        record.lastMessageAtUnixMs = now;
        record.idleDeadlineUnixMs = now + 30_000;
        Messages.ChatMessage message = new Messages.ChatMessage(
            record.conversationId,
            seq,
            request.actorId(),
            request.text(),
            now);
        return new Messages.SendChatMessageRes(message, state(record));
    }

    public synchronized Messages.ConversationState typing(Messages.SetTypingSupportReq request) {
        return state(require(request.conversationId()));
    }

    public synchronized Messages.CloseConversationRes close(Messages.CloseConversationSupportReq request) {
        ConversationRecord record = require(request.conversationId());
        record.status = "Closed";
        return new Messages.CloseConversationRes(state(record));
    }

    public synchronized Messages.ServerAssertionResponse assertConversation(Messages.ServerAssertionRequest request) {
        ConversationRecord record = conversations.get(request.conversationId());
        if (record == null) {
            return new Messages.ServerAssertionResponse(false, "conversation not found");
        }
        if (record.lastMessageSeq < 2) {
            return new Messages.ServerAssertionResponse(false, "expected at least two messages");
        }
        if (!"Closed".equals(record.status)) {
            return new Messages.ServerAssertionResponse(false, "conversation was not closed");
        }
        return new Messages.ServerAssertionResponse(true, "ok");
    }

    private ConversationRecord require(String conversationId) {
        ConversationRecord record = conversations.get(conversationId);
        if (record == null) {
            throw new IllegalArgumentException("Unknown conversation: " + conversationId);
        }
        return record;
    }

    private static Messages.ConversationState state(ConversationRecord record) {
        return new Messages.ConversationState(
            record.conversationId,
            record.subject,
            record.status,
            record.customerActorId,
            record.agentActorId,
            record.lastMessageSeq,
            record.lastMessageAtUnixMs,
            record.idleDeadlineUnixMs);
    }

    private static final class ConversationRecord {
        private final String conversationId;
        private final String subject;
        private final String customerActorId;
        private String agentActorId;
        private String status;
        private long lastMessageSeq;
        private Long lastMessageAtUnixMs;
        private Long idleDeadlineUnixMs;

        private ConversationRecord(
            String conversationId,
            String subject,
            String status,
            String customerActorId,
            String agentActorId) {
            this.conversationId = conversationId;
            this.subject = subject;
            this.status = status;
            this.customerActorId = customerActorId;
            this.agentActorId = agentActorId;
        }
    }
}
