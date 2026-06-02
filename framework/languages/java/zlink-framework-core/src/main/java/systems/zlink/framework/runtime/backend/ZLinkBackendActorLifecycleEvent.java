package systems.zlink.framework.runtime.backend;

public record ZLinkBackendActorLifecycleEvent(
    ZLinkBackendActorLifecycleEventKind kind,
    ZLinkBackendActorLifecycleInfo info) {
}
