package systems.zlink.framework.configuration;

public enum ZLinkDispatchErrorReason {
    HANDLER_MISSING,
    PAYLOAD_DECODE_FAILED,
    HANDLER_EXCEPTION,
    INVALID_FRAME,
    REPLY_PATH_MISSING,
    UNEXPECTED_REPLY
}
