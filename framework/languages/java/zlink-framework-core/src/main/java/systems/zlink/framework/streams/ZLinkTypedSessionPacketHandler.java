package systems.zlink.framework.streams;

import systems.zlink.framework.messaging.ZLinkMessage;

public interface ZLinkTypedSessionPacketHandler<TSessionContext extends ZLinkSessionContext, TMessage>
    extends ZLinkSessionPacketHandler<TSessionContext> {
    Class<TMessage> messageType();

    void handle(TSessionContext context, ZLinkSessionDispatchContext dispatch, TMessage message);

    @Override
    default void handle(TSessionContext context, ZLinkSessionDispatchContext dispatch, ZLinkMessage payload) {
        throw new UnsupportedOperationException(
            "typed session packet handlers are invoked by the framework runtime");
    }
}
