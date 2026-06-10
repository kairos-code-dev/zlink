package systems.zlink.stream.connector;

import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletionStage;

public interface ZLinkStreamTypedRequestCall {
    ZLinkStreamTypedRequestCall packetName(String packetName);

    ZLinkStreamTypedRequestCall metadata(String key, String value);

    ZLinkStreamTypedRequestCall metadata(Map<String, String> metadata);

    ZLinkStreamTypedRequestCall compress();

    ZLinkStreamTypedRequestCall timeout(Duration timeout);

    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);

    default <TReply> TReply await(Class<TReply> replyType) throws Exception {
        return ZLinkStreamCompletions.await(submit(replyType));
    }
}
