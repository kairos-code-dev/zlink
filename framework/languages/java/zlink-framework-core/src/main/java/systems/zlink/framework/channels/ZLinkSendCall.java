package systems.zlink.framework.channels;

import java.util.concurrent.CompletionStage;

public interface ZLinkSendCall {
    ZLinkSendCall packetName(String packetName);

    ZLinkSendCall metadata(String key, String value);

    CompletionStage<Void> submit();
}
