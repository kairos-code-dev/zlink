package systems.zlink.framework.runtime.binding;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.service.spot.SpotNodeMode;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNodeMode;
import systems.zlink.framework.runtime.backend.ZLinkSpotBackendAdapter;

final class ZLinkJavaSpotBackendAdapter implements ZLinkSpotBackendAdapter {
    @Override
    public ZLinkBackendSpotNode createSpotNode(ZLinkBackendContext context, ZLinkBackendSpotNodeMode mode) {
        return new ZLinkJavaSpotNode(nativeContext(context).createSpotNode(map(mode)));
    }

    private static Context nativeContext(ZLinkBackendContext context) {
        return ((ZLinkJavaContext) context).nativeContext();
    }

    private static SpotNodeMode map(ZLinkBackendSpotNodeMode mode) {
        return switch (mode) {
            case PUBSUB -> SpotNodeMode.PUBSUB;
            case ROUTED -> SpotNodeMode.ROUTED;
            case ALL -> SpotNodeMode.ALL;
        };
    }
}
