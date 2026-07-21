package systems.zlink.framework.streams;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkSubmitResult;

public interface ZLinkSessionSendCall {
    ZLinkSessionSendCall metadata(String key, String value);

    ZLinkSessionSendCall compress();

    CompletionStage<ZLinkSubmitResult> submit();
}
