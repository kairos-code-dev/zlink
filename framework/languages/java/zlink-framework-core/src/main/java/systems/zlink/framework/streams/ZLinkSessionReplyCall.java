package systems.zlink.framework.streams;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkSubmitResult;

public interface ZLinkSessionReplyCall {
    ZLinkSessionReplyCall compress();

    CompletionStage<ZLinkSubmitResult> submit();
}
