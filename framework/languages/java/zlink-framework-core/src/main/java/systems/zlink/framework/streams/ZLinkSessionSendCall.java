package systems.zlink.framework.streams;

import java.util.concurrent.CompletionStage;

public interface ZLinkSessionSendCall {
    ZLinkSessionSendCall metadata(String key, String value);

    ZLinkSessionSendCall packetName(String messageName);

    ZLinkSessionSendCall compress();

    default CompletionStage<Void> submit() {
        return submitAsync();
    }

    CompletionStage<Void> submitAsync();
}
