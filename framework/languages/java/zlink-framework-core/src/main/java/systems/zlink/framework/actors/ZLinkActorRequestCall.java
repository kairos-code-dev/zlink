package systems.zlink.framework.actors;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkAwait;

public interface ZLinkActorRequestCall {
    ZLinkActorRequestCall packetName(String packetName);

    ZLinkActorRequestCall timeout(Duration timeout);

    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);

    default <TReply> TReply await(Class<TReply> replyType) {
        return ZLinkAwait.await(submit(replyType));
    }
}
