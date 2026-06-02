package systems.zlink.framework.runtime;

import systems.zlink.contracts.core.RoutingId;

public interface ZLinkBackendStreamErrorHandler {
    void handle(RoutingId routingId, int nativeCode, String message);
}
