package systems.zlink.framework.configuration;

import java.util.concurrent.CompletionStage;

public interface ZLinkMessageDispatchErrorObserver {
    CompletionStage<Void> onDispatchError(ZLinkMessageDispatchErrorEvent error);
}
