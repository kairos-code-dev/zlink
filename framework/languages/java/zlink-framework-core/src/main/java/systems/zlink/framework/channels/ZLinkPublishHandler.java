package systems.zlink.framework.channels;

public interface ZLinkPublishHandler<TMessage> {
    void handle(
        TMessage message,
        ZLinkPublishContext context);
}
