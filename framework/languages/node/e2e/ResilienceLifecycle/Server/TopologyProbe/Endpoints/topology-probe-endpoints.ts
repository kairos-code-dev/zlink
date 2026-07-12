import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  ZLinkLocationTopologyState,
  type ZLinkLocationRuntimeQuery
} from '@zlink-systems/framework';
import type { ServerOptions } from '../Configuration/server-options';
import type { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/types';

export function createTopologyProbeEndpoints(
  options: ServerOptions,
  locationQuery: ZLinkLocationRuntimeQuery,
  evidence: EvidenceStore,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: options.role, rid: options.rid }) },
    {
      method: 'GET',
      path: '/topology-probe/topology',
      handle: async () => {
        const rows = await locationQuery.listPeerLocations({
          autoConnectType: ZLinkLocationAutoConnectType.ClientServer,
          meshName: 'profile',
          role: ZLinkLocationRole.Router
        });
        return rows.map((row) => ({
          channelName: row.meshName,
          serviceRole: row.role,
          state: ZLinkLocationTopologyState.Ready,
          routingId: String(row.nodeRid),
          rid: String(row.nodeRid),
          endpoint: row.endpoint
        }));
      }
    },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    { method: 'POST', path: '/evidence/clear', handle: () => { evidence.clear(); return { status: 'cleared' }; } },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}
