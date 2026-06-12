package systems.zlink.framework.channels;

public interface ZLinkRouteSendHandler<TMessage> {
    void handle(
        TMessage message,
        ZLinkRouteSendContext context);
}
