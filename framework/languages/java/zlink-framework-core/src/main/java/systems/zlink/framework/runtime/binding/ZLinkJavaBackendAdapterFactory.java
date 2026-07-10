package systems.zlink.framework.runtime.binding;

import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.backend.ZLinkChannelBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkMonitoringBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkSpotBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkStreamBackendAdapter;

public final class ZLinkJavaBackendAdapterFactory implements ZLinkBackendAdapterFactory {
    @Override
    public ZLinkChannelBackendAdapter createChannelAdapter(ZLinkBackendAdapterOptions options) {
        return new ZLinkJavaChannelBackendAdapter();
    }

    @Override
    public ZLinkSpotBackendAdapter createSpotAdapter(ZLinkBackendAdapterOptions options) {
        return new ZLinkJavaSpotBackendAdapter();
    }

    @Override
    public ZLinkStreamBackendAdapter createStreamAdapter(ZLinkBackendAdapterOptions options) {
        return new ZLinkJavaStreamBackendAdapter();
    }

    @Override
    public ZLinkMonitoringBackendAdapter createMonitoringAdapter(ZLinkBackendAdapterOptions options) {
        return socket -> new ZLinkJavaSocketMonitor(((ZLinkJavaSocketBacked) socket).nativeSocket().monitorOpen());
    }
}
