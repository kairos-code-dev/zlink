import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  type ZLinkLocationRuntimeQuery
} from '@zlink-systems/framework';
import { PubSubNames, type EvidenceWaitReq } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';

export function createSubscriberEndpoints(
  evidence: EvidenceStore,
  locations: ZLinkLocationRuntimeQuery,
  stop: () => void
): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'subscriber', rid: evidence.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/evidence/wait',
      handle: async (body) => {
        const request = body as EvidenceWaitReq;
        const timeout = clamp(request.timeoutMilliseconds ?? 10_000, 1, 30_000);
        return await evidence.waitUntil((entries) => matches(entries.slice(request.afterIndex ?? 0), request), timeout);
      }
    },
    { method: 'GET', path: '/locations/peers', handle: () => publisherObservations(locations) },
    { method: 'POST', path: '/evidence/clear', handle: () => { evidence.clear(); return { status: 'cleared' }; } },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

function publisherRows(locations: ZLinkLocationRuntimeQuery) {
  return locations.listPeerLocations({
    autoConnectType: ZLinkLocationAutoConnectType.Fanout,
    meshName: PubSubNames.channel,
    role: ZLinkLocationRole.Pub
  });
}

async function publisherObservations(locations: ZLinkLocationRuntimeQuery) {
  return (await publisherRows(locations)).map((row) => ({
    rid: routingIdText(row.nodeRid),
    endpoint: row.endpoint
  }));
}

function routingIdText(value: unknown): string {
  if (typeof value === 'string') return value;
  const text = String(value);
  return text === '[object Object]' ? '' : text;
}

function matches(entries: readonly string[], request: EvidenceWaitReq): boolean {
  const containsAll = request.containsAll ?? [];
  const containsAnyGroups = request.containsAnyGroups ?? [];
  const containsAllLineGroups = request.containsAllLineGroups ?? [];
  const containsAnyLineGroups = request.containsAnyLineGroups ?? [];
  return containsAll.every((expected) => entries.some((entry) => entry.includes(expected)))
    && containsAnyGroups.every((group) => group.some((expected) => entries.some((entry) => entry.includes(expected))))
    && containsAllLineGroups.every((group) => entries.some((entry) => group.every((expected) => entry.includes(expected))))
    && (containsAnyLineGroups.length === 0
      || containsAnyLineGroups.some((group) => entries.some((entry) => group.every((expected) => entry.includes(expected)))));
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}
