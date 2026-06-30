import type { EvidenceWaitRequest } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';

export function createSubscriberEndpoints(evidence: EvidenceStore, stop: () => void): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'subscriber', rid: evidence.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/evidence/wait',
      handle: async (body) => {
        const request = body as EvidenceWaitRequest;
        const timeout = clamp(request.timeoutMilliseconds ?? 10_000, 1, 30_000);
        return await evidence.waitUntil((entries) => matches(entries, request), timeout);
      }
    },
    { method: 'POST', path: '/evidence/clear', handle: () => { evidence.clear(); return { status: 'cleared' }; } },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

function matches(entries: readonly string[], request: EvidenceWaitRequest): boolean {
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
