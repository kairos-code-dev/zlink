package systems.zlink.framework.channels;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkRouteRequestCall {
    ZLinkRouteRequestCall packetName(String packetName);

    ZLinkRouteRequestCall metadata(String key, String value);

    ZLinkRouteRequestCall timeout(Duration timeout);

    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);

    default <TReply> TReply await(Class<TReply> replyType) {
        return ZLinkAwait.await(submit(replyType));
    }
}
