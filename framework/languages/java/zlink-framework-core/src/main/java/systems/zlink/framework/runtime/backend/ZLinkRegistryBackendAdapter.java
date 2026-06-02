package systems.zlink.framework.runtime.backend;

public interface ZLinkRegistryBackendAdapter {
    ZLinkBackendRegistry createRegistry(ZLinkBackendContext context);

    ZLinkBackendRegistryQueryClient createRegistryQueryClient(ZLinkBackendContext context);
}
