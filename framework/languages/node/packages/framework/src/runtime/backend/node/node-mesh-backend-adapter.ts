import type {
  ZLinkBackendContext,
  ZLinkBackendMeshNode,
  ZLinkMeshBackendAdapter
} from '../contracts';
import {
  zlink
} from './node-backend-adapter-support';

export class ZLinkNodeMeshBackendAdapter implements ZLinkMeshBackendAdapter {
  createMeshNode(
    context: ZLinkBackendContext,
    options: {
      readonly meshName: string;
      readonly routingId?: string;
      readonly trustProfile?: string;
    }
  ): ZLinkBackendMeshNode {
    const node = zlink.createMeshNode(
      context.nativeInstance as Parameters<typeof zlink.createMeshNode>[0],
      {
        meshName: options.meshName,
        ...(options.trustProfile === undefined ? {} : { trustProfile: options.trustProfile })
      }
    );
    if (options.routingId !== undefined) {
      node.setRoutingId(zlink.RoutingId.from(options.routingId));
    }
    return node;
  }
}
