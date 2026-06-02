package systems.zlink.framework.spring;

@FunctionalInterface
public interface ZLinkRegistryQueryClientCustomizer {
    void customize(ZLinkRegistryQueryClientOptions options);
}
