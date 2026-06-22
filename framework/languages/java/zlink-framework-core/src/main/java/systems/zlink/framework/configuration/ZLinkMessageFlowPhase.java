package systems.zlink.framework.configuration;

// A success-path transition in a message's lifecycle. Errors are reported separately
// via ZLinkMessageDispatchErrorEvent. RECEIVED/DISPATCHED/REPLIED are inbound (this
// node receives); SENT/REPLY_RECEIVED are outbound (this node sends).
public enum ZLinkMessageFlowPhase {
    RECEIVED,
    DISPATCHED,
    REPLIED,
    DROPPED,
    SENT,
    REPLY_RECEIVED
}
