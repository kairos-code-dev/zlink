package systems.zlink.framework.streams;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActorRef;

public interface ZLinkSessionActor {
    String actorId();

    ZLinkActorRef ref();

    CompletionStage<Void> relay(ZLinkStreamHeader header, Message payload);

    CompletionStage<Void> notifyDisconnected();
}
