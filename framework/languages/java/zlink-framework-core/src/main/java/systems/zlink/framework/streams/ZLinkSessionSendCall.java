package systems.zlink.framework.streams;

import java.util.concurrent.CompletionStage;

public interface ZLinkSessionSendCall {
    ZLinkSessionSendCall metadata(String key, String value);

    ZLinkSessionSendCall packetName(String messageName);

    ZLinkSessionSendCall compress();

    CompletionStage<Void> submit();
}
