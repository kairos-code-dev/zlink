package systems.zlink.stream.connector;

import java.util.Map;
import java.util.concurrent.CompletionStage;

public interface ZLinkStreamSendCall {
    ZLinkStreamSendCall packetName(String packetName);

    ZLinkStreamSendCall metadata(String key, String value);

    ZLinkStreamSendCall metadata(Map<String, String> metadata);

    ZLinkStreamSendCall compress();

    CompletionStage<Void> submit();

    default void await() throws Exception {
        ZLinkStreamCompletions.await(submit());
    }
}
