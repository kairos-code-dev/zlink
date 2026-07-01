package systems.zlink.framework.streams;

import systems.zlink.framework.ZLinkSubmitStage;

public interface ZLinkSessionReplyCall {
    ZLinkSessionReplyCall metadata(String key, String value);

    ZLinkSessionReplyCall compress();

    ZLinkSubmitStage submit();

    default void await() {
        submit().await();
    }
}
