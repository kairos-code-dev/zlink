package systems.zlink.framework.streams;

import systems.zlink.contracts.messaging.Message;

public interface ZLinkSession {
    ZLinkSessionContext context();

    void onConnected();

    void onDisconnected();

    void onError(ZLinkStreamError error);

    default void onDispatch(
        ZLinkStreamHeader header,
        Message payload) {
    }
}
