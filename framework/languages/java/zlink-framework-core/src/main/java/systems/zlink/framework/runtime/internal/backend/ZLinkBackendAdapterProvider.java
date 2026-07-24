package systems.zlink.framework.runtime.internal.backend;

import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.backend.ZLinkChannelBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkMonitoringBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkMeshBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkSpotBackendAdapter;
import systems.zlink.framework.runtime.backend.ZLinkStreamBackendAdapter;

public interface ZLinkBackendAdapterProvider {
    ZLinkChannelBackendAdapter createChannelAdapter(ZLinkBackendAdapterOptions options);

    ZLinkSpotBackendAdapter createSpotAdapter(ZLinkBackendAdapterOptions options);

    default ZLinkMeshBackendAdapter createMeshAdapter(ZLinkBackendAdapterOptions options) {
        throw new UnsupportedOperationException("MeshNode backend is not available");
    }

    ZLinkStreamBackendAdapter createStreamAdapter(ZLinkBackendAdapterOptions options);

    ZLinkMonitoringBackendAdapter createMonitoringAdapter(ZLinkBackendAdapterOptions options);

    default java.util.function.Function<
        systems.zlink.framework.runtime.backend.ZLinkBackendObject,
        systems.zlink.framework.runtime.backend.ZLinkBackendObject> admissionSource() {
        return backend -> backend;
    }

    default java.util.function.Function<
        systems.zlink.framework.runtime.backend.ZLinkBackendObject,
        java.time.Duration> admissionTimeout() {
        return ignored -> java.time.Duration.ofSeconds(1);
    }

    default java.util.function.ToIntFunction<
        systems.zlink.framework.runtime.backend.ZLinkBackendObject>
        admissionPendingCapacity() {
        return ignored -> 4096;
    }

    default java.util.function.BiConsumer<
        systems.zlink.framework.runtime.backend.ZLinkBackendObject,
        java.util.function.Consumer<
            systems.zlink.framework.runtime.backend.ZLinkBackendAdmissionKey>>
        admissionReadyRegistrar() {
        return (ignored, handler) -> { };
    }

    default java.util.function.BiConsumer<
        systems.zlink.framework.runtime.backend.ZLinkBackendObject,
        Runnable> admissionShutdownRegistrar() {
        return (ignored, handler) -> { };
    }
}
