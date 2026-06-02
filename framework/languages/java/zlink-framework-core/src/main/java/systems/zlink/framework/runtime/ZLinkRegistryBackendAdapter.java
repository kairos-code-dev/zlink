package systems.zlink.framework.runtime;

public interface ZLinkRegistryBackendAdapter {
    ZLinkBackendRegistry createRegistry(ZLinkBackendContext context);

    ZLinkBackendRegistryQueryClient createRegistryQueryClient(ZLinkBackendContext context);
}
