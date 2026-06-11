package systems.zlink.framework.actors;

import java.util.concurrent.CompletionStage;

public interface ZLinkBoundSessionSendCall {
    ZLinkBoundSessionSendCall packetName(String packetName);

    ZLinkBoundSessionSendCall metadata(String key, String value);

    CompletionStage<Void> submit();
}
