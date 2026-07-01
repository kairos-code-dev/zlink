package systems.zlink.framework.channels;

import systems.zlink.framework.ZLinkSubmitStage;

public interface ZLinkPublishCall {
    ZLinkPublishCall packetName(String packetName);

    ZLinkPublishCall metadata(String key, String value);

    ZLinkSubmitStage submit();

    default void await() {
        submit().await();
    }
}
