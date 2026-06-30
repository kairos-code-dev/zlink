import type { EvidenceWaitReq } from '../../../Shared/messages';
import { RuntimeMonitoringNames } from '../../../Shared/messages';
import type { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';
import type { ZLinkChannelRuntimeOptions } from '@zlink-systems/framework';

export function createServiceEndpoints(
  evidence: EvidenceStore,
  runtimeOptions: ZLinkChannelRuntimeOptions,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'service', rid: evidence.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/admin/drain',
      handle: () => {
        runtimeOptions.clientServerChannel(RuntimeMonitoringNames.channel).configureServerSocket().weight = 0;
        evidence.add(`admin|rid=${evidence.rid}|action=drain|weight=0`);
        return { status: 'drained', weight: 0 };
      }
    },
    {
      method: 'POST',
      path: '/admin/restore',
      handle: () => {
        runtimeOptions.clientServerChannel(RuntimeMonitoringNames.channel).configureServerSocket().weight = 100;
        evidence.add(`admin|rid=${evidence.rid}|action=restore|weight=100`);
        return { status: 'restored', weight: 100 };
      }
    },
    {
      method: 'GET',
      path: '/admin/weight',
      handle: () => ({
        weight: runtimeOptions.clientServerChannel(RuntimeMonitoringNames.channel).configureServerSocket().weight
      })
    },
    {
      method: 'POST',
      path: '/evidence/wait',
      handle: (body) => {
        const request = body as EvidenceWaitReq;
        const timeout = Math.max(1, Math.min(request.timeoutMilliseconds ?? 10000, 30000));
        return evidence.waitUntil((entries) =>
          request.containsAll.every((expected) => entries.some((entry) => entry.includes(expected)))
          && request.containsAnyGroups.every((group) => group.some((expected) =>
            entries.some((entry) => entry.includes(expected)))), timeout);
      }
    },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}
