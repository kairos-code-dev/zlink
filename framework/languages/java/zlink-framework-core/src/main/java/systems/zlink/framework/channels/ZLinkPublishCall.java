package systems.zlink.framework.channels;

import java.util.Map;
import java.util.concurrent.CompletionStage;

public interface ZLinkPublishCall {
    default ZLinkPublishCall metadata(String key, String value) {
        throw new UnsupportedOperationException("publish metadata is not available");
    }

    default ZLinkPublishCall metadata(Map<String, String> metadata) {
        throw new UnsupportedOperationException("publish metadata is not available");
    }

    CompletionStage<ZLinkPublishResult> submit();
}
