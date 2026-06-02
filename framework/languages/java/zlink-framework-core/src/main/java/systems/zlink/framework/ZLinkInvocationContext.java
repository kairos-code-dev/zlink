package systems.zlink.framework;

import java.util.Optional;

public interface ZLinkInvocationContext extends ZLinkHandlerContext {
    Optional<Object> request();
}
