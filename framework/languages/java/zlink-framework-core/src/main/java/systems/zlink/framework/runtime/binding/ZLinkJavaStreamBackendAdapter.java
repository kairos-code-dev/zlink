package systems.zlink.framework.runtime.binding;

import systems.zlink.contracts.core.Context;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamSocket;
import systems.zlink.framework.runtime.backend.ZLinkStreamBackendAdapter;

final class ZLinkJavaStreamBackendAdapter implements ZLinkStreamBackendAdapter {
    @Override
    public ZLinkBackendStreamSocket createStreamSocket(
        ZLinkBackendContext context,
        systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode meshNode) {
        return new ZLinkJavaStreamSocket(
            ZLinkJavaSocketOptions.configureFrameworkSocket(
                nativeContext(context).createStreamSocket()),
            meshNode == null ? null : (ZLinkJavaMeshNode) meshNode);
    }

    private static Context nativeContext(ZLinkBackendContext context) {
        return ((ZLinkJavaContext) context).nativeContext();
    }
}
