package systems.zlink.framework.streams;

import systems.zlink.framework.ZLinkSubmitStage;

public interface ZLinkSessionSendCall {
    ZLinkSessionSendCall metadata(String key, String value);

    ZLinkSessionSendCall packetName(String messageName);

    ZLinkSessionSendCall compress();

    ZLinkSubmitStage submit();

    default void await() {
        submit().await();
    }
}
