package systems.zlink.framework.actors;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkBoundSession {
    ZLinkBoundSessionSendCall send(Message message);

    CompletionStage<Void> disconnect();
}
