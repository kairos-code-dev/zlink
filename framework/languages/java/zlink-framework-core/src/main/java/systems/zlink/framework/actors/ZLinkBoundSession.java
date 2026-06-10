package systems.zlink.framework.actors;

import java.util.concurrent.CompletionStage;

public interface ZLinkBoundSession {
    <TMessage> ZLinkBoundSessionSendCall send(TMessage message);

    default CompletionStage<Void> disconnect() {
        return disconnectAsync();
    }

    CompletionStage<Void> disconnectAsync();
}
