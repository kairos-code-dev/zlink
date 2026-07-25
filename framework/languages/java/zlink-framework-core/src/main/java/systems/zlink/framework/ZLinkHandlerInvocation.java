package systems.zlink.framework;

import java.util.Optional;

public interface ZLinkHandlerInvocation {
    ZLinkMessageContext messageContext();

    Optional<Object> request();
}
