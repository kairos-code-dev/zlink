package systems.zlink.stream.connector;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkStreamRequestCall {
    ZLinkStreamRequestCall packetName(String packetName);

    ZLinkStreamRequestCall metadata(String key, String value);

    ZLinkStreamRequestCall timeout(Duration timeout);

    CompletionStage<ZLinkStreamEncodedPayload> submitAsync();
}
