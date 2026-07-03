import { ZLinkLocationAutoConnectType, ZLinkLocationRole, ZLinkLocationTopologyState } from '@zlink-systems/framework';
import type { IZLinkLocationStore } from '@zlink-systems/framework';
import type { ServerOptions } from '../Configuration/server-options';
import type { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/types';

export function createLocationProbeEndpoints(
  options: ServerOptions,
  store: IZLinkLocationStore,
  evidence: EvidenceStore,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: "location-probe", rid: options.rid }) },
    {
      method: 'GET',
      path: '/location/topology',
      handle: async () => {
        const rows = await store.listPeers({
          autoConnectType: ZLinkLocationAutoConnectType.ClientServer,
          meshName: 'profile',
          role: ZLinkLocationRole.Router
        });
        return rows.map((row) => ({
          channelName: row.meshName,
          serviceRole: ZLinkLocationRole.Router,
          state: ZLinkLocationTopologyState.Ready,
          routingId: row.nodeRid,
          endpoint: row.endpoint
        }));
      }
    },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    { method: 'POST', path: '/evidence/clear', handle: () => { evidence.clear(); return { status: 'cleared' }; } },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}
