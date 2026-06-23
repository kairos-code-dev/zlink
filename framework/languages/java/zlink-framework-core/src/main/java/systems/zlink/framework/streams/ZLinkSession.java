package systems.zlink.framework.streams;

import systems.zlink.framework.messaging.ZLinkMessage;

public interface ZLinkSession {
    ZLinkSessionContext context();

    void onConnected();

    void onDisconnected();

    void onError(ZLinkStreamError error);

    default void onDispatch(
        ZLinkStreamHeader header,
        ZLinkMessage payload) {
    }
}
