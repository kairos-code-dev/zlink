package systems.zlink.framework.channels;

import java.util.concurrent.CompletionStage;

public interface ZLinkPublishCall {
    ZLinkPublishCall packetName(String packetName);

    ZLinkPublishCall metadata(String key, String value);

    CompletionStage<Void> submitAsync();
}
