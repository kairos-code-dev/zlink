package systems.zlink.framework.channels;

import systems.zlink.framework.ZLinkSubmitStage;

public interface ZLinkSendCall {
    ZLinkSendCall packetName(String packetName);

    ZLinkSendCall metadata(String key, String value);

    ZLinkSubmitStage submit();

    default void await() {
        submit().await();
    }
}
