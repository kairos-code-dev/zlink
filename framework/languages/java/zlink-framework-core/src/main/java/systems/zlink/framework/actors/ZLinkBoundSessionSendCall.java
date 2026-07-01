package systems.zlink.framework.actors;

import systems.zlink.framework.ZLinkSubmitStage;

public interface ZLinkBoundSessionSendCall {
    ZLinkBoundSessionSendCall packetName(String packetName);

    ZLinkBoundSessionSendCall metadata(String key, String value);

    ZLinkSubmitStage submit();

    default void await() {
        submit().await();
    }
}
