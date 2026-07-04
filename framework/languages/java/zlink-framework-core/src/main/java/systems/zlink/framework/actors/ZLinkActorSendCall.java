package systems.zlink.framework.actors;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkActorSendCall {
    ZLinkActorSendCall packetName(String packetName);

    CompletionStage<Void> submit();

    default void await() {
        ZLinkAwait.awaitVoid(submit());
    }
}
