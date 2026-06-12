package systems.zlink.framework.channels;

public interface ZLinkSendHandler<TMessage> {
    void handle(
        TMessage message,
        ZLinkSendContext context);
}
