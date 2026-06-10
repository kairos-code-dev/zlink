package systems.zlink.framework.channels;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

public interface ZLinkRequestCall {
    ZLinkRequestCall packetName(String packetName);

    ZLinkRequestCall metadata(String key, String value);

    ZLinkRequestCall timeout(Duration timeout);

    default <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
        return submitAsync(replyType);
    }

    <TReply> CompletionStage<TReply> submitAsync(Class<TReply> replyType);
}
