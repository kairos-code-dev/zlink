import {
  ZLinkServiceRole,
  ZLinkTopologyState,
  type ZLinkRegistryQuery
} from '@zlink-systems/framework';
import type { EvidenceWaitReq } from '../../../Shared/messages';
import { EvidenceStore } from '../../Provider/Infrastructure/evidence-store';
import type { EmbeddedOptions } from '../Configuration/embedded-options';
import type { HttpRoute } from '../../Registry/Support/http-server';

export function createEmbeddedEndpoints(
  options: EmbeddedOptions,
  query: ZLinkRegistryQuery,
  evidence: EvidenceStore,
  stop: () => void
): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'embedded', rid: options.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/evidence/wait',
      handle: (body) => {
        const request = body as EvidenceWaitReq;
        return evidence.waitUntil(request.contains, Math.max(1, Math.min(request.timeoutMilliseconds ?? 10000, 30000)));
      }
    },
    { method: 'GET', path: '/registry/status', handle: () => query.status() },
    {
      method: 'GET',
      path: '/registry/topology',
      handle: () => query.topology({
        channelName: 'profile',
        serviceRole: ZLinkServiceRole.Router,
        state: ZLinkTopologyState.Ready
      })
    },
    { method: 'GET', path: '/registry/member-peers', handle: () => query.memberPeers('profile') },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}
