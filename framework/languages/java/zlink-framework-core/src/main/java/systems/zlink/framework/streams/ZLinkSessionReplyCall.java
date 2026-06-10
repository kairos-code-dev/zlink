package systems.zlink.framework.streams;

import java.util.concurrent.CompletionStage;

public interface ZLinkSessionReplyCall {
    ZLinkSessionReplyCall metadata(String key, String value);

    ZLinkSessionReplyCall compress();

    default CompletionStage<Void> submit() {
        return submitAsync();
    }

    CompletionStage<Void> submitAsync();
}
