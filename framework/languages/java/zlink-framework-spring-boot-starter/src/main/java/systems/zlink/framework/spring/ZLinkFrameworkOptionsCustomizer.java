package systems.zlink.framework.spring;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;

@FunctionalInterface
public interface ZLinkFrameworkOptionsCustomizer {
    void customize(ZLinkFrameworkOptions options);
}
