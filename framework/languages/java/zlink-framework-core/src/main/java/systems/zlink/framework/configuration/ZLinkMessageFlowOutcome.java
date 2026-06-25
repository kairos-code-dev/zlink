package systems.zlink.framework.configuration;

// A transition or dispatch error outcome in a message's lifecycle.
// RECEIVED/DISPATCHED/REPLIED are inbound (this node receives);
// SENT/REPLY_RECEIVED are outbound (this node sends).
public enum ZLinkMessageFlowOutcome {
    RECEIVED,
    DISPATCHED,
    REPLIED,
    DROPPED,
    SENT,
    REPLY_RECEIVED,
    ERROR
}
