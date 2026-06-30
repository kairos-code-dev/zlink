import type { ZLinkSpotManager, ZLinkSpotOutbound } from '@zlink-systems/framework';
import type {
  EvidenceWaitReq,
  MultiNodeCreateSpotReq,
  MultiNodeStateRouteReq
} from '../../../Shared/messages';
import type { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';
import { createLocalMultiNodeSpot, requestStateViaSpotOutboundWithRetry } from '../Spots/multi-node-spots';

export function createMultiNodeEndpoints(
  evidence: EvidenceStore,
  spots: ZLinkSpotManager,
  outbound: ZLinkSpotOutbound,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'multi-node', rid: evidence.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/evidence/wait',
      handle: (body) => {
        const request = body as EvidenceWaitReq;
        const timeout = Math.max(1, Math.min(request.timeoutMilliseconds ?? 10000, 30000));
        return evidence.waitUntil((entries) =>
          request.containsAll.every((expected) => entries.some((entry) => entry.includes(expected))), timeout);
      }
    },
    {
      method: 'POST',
      path: '/spot/create-local',
      handle: (body) => {
        const request = body as MultiNodeCreateSpotReq;
        return createLocalMultiNodeSpot(spots, evidence, evidence.rid, request.spotRid);
      }
    },
    {
      method: 'POST',
      path: '/spot/state/request',
      handle: (body) => {
        const request = body as MultiNodeStateRouteReq;
        return requestStateViaSpotOutboundWithRetry(outbound, request.spotRid, request.delta);
      }
    },
    {
      method: 'POST',
      path: '/shutdown',
      handle: () => {
        stop();
        return { status: 'stopping' };
      }
    },
    {
      method: 'POST',
      path: '/crash',
      handle: () => {
        setTimeout(() => process.exit(1), 10);
        return { status: 'crashing' };
      }
    }
  ];
}
