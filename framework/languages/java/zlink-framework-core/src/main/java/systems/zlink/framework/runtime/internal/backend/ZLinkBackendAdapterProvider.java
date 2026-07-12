package systems.zlink.framework.runtime.internal.backend;

import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.backend.ZLinkChannelBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkMonitoringBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkSpotBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkStreamBackendAdapter;

public interface ZLinkBackendAdapterProvider {
    ZLinkChannelBackendAdapter createChannelAdapter(ZLinkBackendAdapterOptions options);

    ZLinkSpotBackendAdapter createSpotAdapter(ZLinkBackendAdapterOptions options);

    ZLinkStreamBackendAdapter createStreamAdapter(ZLinkBackendAdapterOptions options);

    ZLinkMonitoringBackendAdapter createMonitoringAdapter(ZLinkBackendAdapterOptions options);
}
