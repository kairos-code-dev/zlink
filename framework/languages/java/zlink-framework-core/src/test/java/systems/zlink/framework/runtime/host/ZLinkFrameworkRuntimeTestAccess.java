package systems.zlink.framework.runtime.host;

import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;

/** Test-only access to the package-owned runtime bootstrap. */
public final class ZLinkFrameworkRuntimeTestAccess {
    private ZLinkFrameworkRuntimeTestAccess() {
    }

    public static ZLinkFrameworkRuntime start(
        DefaultZLinkFrameworkOptions options) {
        return ZLinkFrameworkRuntime.start(
            options,
            new ZLinkJavaBackendAdapterFactory());
    }
}
