package systems.zlink.framework.channels;

import java.util.Optional;
import systems.zlink.framework.ZLinkHandlerContext;

public interface ZLinkPublishContext extends ZLinkHandlerContext {
    String topic();

    Optional<String> source();
}
