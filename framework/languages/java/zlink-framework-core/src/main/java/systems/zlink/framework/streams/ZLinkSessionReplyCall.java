package systems.zlink.framework.streams;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkSessionReplyCall {
    ZLinkSessionReplyCall metadata(String key, String value);

    ZLinkSessionReplyCall compress();

    CompletionStage<Void> submit();

    default void await() {
        ZLinkAwait.awaitVoid(submit());
    }
}
