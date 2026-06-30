import { ZLinkServiceRole, ZLinkTopologyState } from '@zlink-systems/framework';
import type { ZLinkRegistryQuery } from '@zlink-systems/framework';
import type { ServerOptions } from '../Configuration/server-options';
import type { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/types';

export function createRegistryEndpoints(
  options: ServerOptions,
  query: ZLinkRegistryQuery,
  evidence: EvidenceStore,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: options.role, rid: options.rid }) },
    {
      method: 'GET',
      path: '/registry/topology',
      handle: async () => query.topology({
        channelName: 'profile',
        serviceRole: ZLinkServiceRole.Router,
        state: ZLinkTopologyState.Ready
      })
    },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    { method: 'POST', path: '/evidence/clear', handle: () => { evidence.clear(); return { status: 'cleared' }; } },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}
