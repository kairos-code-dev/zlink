package systems.zlink.framework.streams;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.messaging.ZLinkMessage;

public interface ZLinkSessionActor {
    String actorId();

    ZLinkActorRef ref();

    CompletionStage<Void> relay(ZLinkMessage payload);

    CompletionStage<Void> notifyDisconnected();
}
