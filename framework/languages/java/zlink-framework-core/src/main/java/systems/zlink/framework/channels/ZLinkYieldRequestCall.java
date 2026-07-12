package systems.zlink.framework.channels;

import java.time.Duration;

public interface ZLinkYieldRequestCall extends ZLinkRequestCall {
    @Override
    ZLinkYieldRequestCall metadata(String key, String value);

    @Override
    ZLinkYieldRequestCall timeout(Duration timeout);

    <TReply> TReply yield(Class<TReply> replyType);

}
