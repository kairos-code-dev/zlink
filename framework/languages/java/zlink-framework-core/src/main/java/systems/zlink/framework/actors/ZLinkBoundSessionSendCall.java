package systems.zlink.framework.actors;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkSubmitResult;

public interface ZLinkBoundSessionSendCall {
    ZLinkBoundSessionSendCall metadata(String key, String value);

    CompletionStage<ZLinkSubmitResult> submit();
}
