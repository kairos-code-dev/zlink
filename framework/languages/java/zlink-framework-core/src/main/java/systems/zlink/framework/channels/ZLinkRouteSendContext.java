package systems.zlink.framework.channels;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkHandlerContext;

public interface ZLinkRouteSendContext extends ZLinkHandlerContext {
    RoutingId routingId();
}
