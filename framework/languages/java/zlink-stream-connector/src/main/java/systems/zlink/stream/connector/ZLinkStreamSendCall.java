package systems.zlink.stream.connector;

import java.util.concurrent.CompletionStage;

public interface ZLinkStreamSendCall {
    ZLinkStreamSendCall packetName(String packetName);

    ZLinkStreamSendCall metadata(String key, String value);

    CompletionStage<Void> submitAsync();
}
