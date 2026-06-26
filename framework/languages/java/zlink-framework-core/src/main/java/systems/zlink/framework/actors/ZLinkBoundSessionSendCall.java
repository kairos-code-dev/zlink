package systems.zlink.framework.actors;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkBoundSessionSendCall {
    ZLinkBoundSessionSendCall packetName(String packetName);

    ZLinkBoundSessionSendCall metadata(String key, String value);

    CompletionStage<Void> submit();

    default void yield() {
        throw new UnsupportedOperationException(
            "yield is not supported by this bound session send call");
    }

    default void await() {
        ZLinkAwait.awaitVoid(submit());
    }

}
