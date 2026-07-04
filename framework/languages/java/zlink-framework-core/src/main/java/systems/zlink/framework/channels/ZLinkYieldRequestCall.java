package systems.zlink.framework.channels;

import java.time.Duration;
import systems.zlink.framework.CancellationToken;

public interface ZLinkYieldRequestCall extends ZLinkRequestCall {
    @Override
    ZLinkYieldRequestCall packetName(String packetName);

    @Override
    ZLinkYieldRequestCall metadata(String key, String value);

    @Override
    ZLinkYieldRequestCall timeout(Duration timeout);

    <TReply> TReply yield(Class<TReply> replyType);

    <TReply> TReply yield(Class<TReply> replyType, CancellationToken cancellationToken);
}
