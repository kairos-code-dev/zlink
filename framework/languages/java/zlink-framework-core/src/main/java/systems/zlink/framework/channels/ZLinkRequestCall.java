package systems.zlink.framework.channels;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkRequestCall {
    ZLinkRequestCall packetName(String packetName);

    ZLinkRequestCall metadata(String key, String value);

    ZLinkRequestCall timeout(Duration timeout);

    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);

    default <TReply> TReply yield(Class<TReply> replyType) {
        throw new UnsupportedOperationException(
            "yield is not supported by this request call");
    }

    default <TReply> TReply await(Class<TReply> replyType) {
        return ZLinkAwait.await(submit(replyType));
    }

}
