package systems.zlink.framework.actors;

import java.util.concurrent.CompletionStage;

public interface ZLinkBoundSessionSendCall {
    ZLinkBoundSessionSendCall packetName(String packetName);

    ZLinkBoundSessionSendCall metadata(String key, String value);

    default CompletionStage<Void> submit() {
        return submitAsync();
    }

    CompletionStage<Void> submitAsync();
}
