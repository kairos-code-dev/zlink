package systems.zlink.framework.channels;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkSendCall {
    ZLinkSendCall packetName(String packetName);

    ZLinkSendCall metadata(String key, String value);

    CompletionStage<Void> submit();

    default void await() {
        ZLinkAwait.awaitVoid(submit());
    }
}
