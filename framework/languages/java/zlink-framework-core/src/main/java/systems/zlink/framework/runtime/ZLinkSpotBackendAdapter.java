package systems.zlink.framework.runtime;

public interface ZLinkSpotBackendAdapter {
    ZLinkBackendSpotNode createSpotNode(ZLinkBackendContext context, ZLinkBackendSpotNodeMode mode);
}
