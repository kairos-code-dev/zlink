import type { EvidenceWaitReq } from '../../../Shared/messages';
import { RuntimeMonitoringNames } from '../../../Shared/messages';
import type { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';
import type { ZLinkChannelRuntimeOptions } from '@zlink-systems/framework';
import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  type ZLinkRouteMeshRuntime,
  type ZLinkLocationRuntimeQuery
} from '@zlink-systems/framework';

export function createServiceEndpoints(
  evidence: EvidenceStore,
  runtimeOptions: ZLinkChannelRuntimeOptions,
  routeMeshRuntime: ZLinkRouteMeshRuntime,
  locations: ZLinkLocationRuntimeQuery,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'service', rid: evidence.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/admin/drain',
      handle: async () => {
        const result = await routeMeshRuntime.drain(RuntimeMonitoringNames.channel);
        const reason = 'reason' in result ? result.reason : '';
        evidence.add(`admin|rid=${evidence.rid}|action=drain|kind=${result.kind}|reason=${reason}`);
        return result;
      }
    },
    {
      method: 'POST',
      path: '/admin/exclude',
      handle: () => {
        runtimeOptions.clientServerChannel(RuntimeMonitoringNames.channel).configureServerSocket().weight = 0;
        evidence.add(`admin|rid=${evidence.rid}|action=exclude|weight=0`);
        return { status: 'excluded', weight: 0 };
      }
    },
    {
      method: 'POST',
      path: '/admin/include',
      handle: () => {
        runtimeOptions.clientServerChannel(RuntimeMonitoringNames.channel).configureServerSocket().weight = 100;
        evidence.add(`admin|rid=${evidence.rid}|action=include|weight=100`);
        return { status: 'included', weight: 100 };
      }
    },
    {
      method: 'GET',
      path: '/locations/peers',
      handle: async () => (await locations.listPeerLocations({
        autoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
        meshName: RuntimeMonitoringNames.channel,
        role: ZLinkLocationRole.Router
      })).map((row) => ({ rid: String(row.nodeRid), endpoint: row.endpoint }))
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
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } },
    {
      method: 'POST', path: '/crash', handle: () => {
        setTimeout(() => process.kill(process.pid, 'SIGKILL'), 10);
        return { status: 'crashing', signal: 'SIGKILL' };
      }
    }
  ];
}
