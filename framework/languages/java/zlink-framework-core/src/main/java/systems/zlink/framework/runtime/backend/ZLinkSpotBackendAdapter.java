package systems.zlink.framework.runtime.backend;

public interface ZLinkSpotBackendAdapter {
    ZLinkBackendSpotNode createSpotNode(ZLinkBackendContext context, ZLinkBackendSpotNodeMode mode);
}
