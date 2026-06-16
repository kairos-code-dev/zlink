package systems.zlink.samples.supportchat.client.configuration;

import java.time.Duration;

public final class SampleNames {
    public static final String ParticipantJoinedPacket = "ParticipantJoinedNotify";
    public static final String ConversationAssignedPacket = "ConversationAssignedNotify";
    public static final String ChatMessagePacket = "ChatMessageNotify";
    public static final String TypingChangedPacket = "TypingChangedNotify";
    public static final String ConversationIdlePacket = "ConversationIdleNotify";
    public static final String ConversationClosedPacket = "ConversationClosedNotify";

    public static final String CustomerRole = "Customer";
    public static final String AgentRole = "Agent";

    public static final String WaitingForAgent = "WaitingForAgent";
    public static final String Active = "Active";
    public static final String WaitingForClose = "WaitingForClose";
    public static final String Closed = "Closed";

    public static final Duration RequestTimeout = Duration.ofSeconds(10);
    public static final Duration IdleTimeout = Duration.ofSeconds(3);
    public static final Duration CloseGraceTimeout = Duration.ofSeconds(2);

    private SampleNames() {
    }
}
