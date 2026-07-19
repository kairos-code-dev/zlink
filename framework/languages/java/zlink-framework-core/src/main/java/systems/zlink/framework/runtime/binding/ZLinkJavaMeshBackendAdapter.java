package systems.zlink.framework.runtime.binding;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.service.spot.MeshNodeOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendContext;
import systems.zlink.framework.runtime.backend.ZLinkMeshBackendAdapter;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;

final class ZLinkJavaMeshBackendAdapter implements ZLinkMeshBackendAdapter {
    @Override
    public ZLinkInternalMeshNode createMeshNode(
        ZLinkBackendContext context,
        String meshName) {
        return new ZLinkJavaMeshNode(
            nativeContext(context).createMeshNode(
                new MeshNodeOptions(meshName, null)),
            meshName);
    }

    private static Context nativeContext(ZLinkBackendContext context) {
        return ((ZLinkJavaContext) context).nativeContext();
    }
}
