import {
  ZLinkServiceRole,
  ZLinkTopologyState,
  type ZLinkRegistryQuery
} from '@zlink-systems/framework';
import type { RegistryOptions } from '../Configuration/registry-options';
import type { HttpRoute } from '../Support/http-server';

export function createRegistryEndpoints(
  options: RegistryOptions,
  query: ZLinkRegistryQuery,
  stop: () => void
): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'registry', rid: options.rid }) },
    { method: 'GET', path: '/registry/status', handle: () => query.status() },
    { method: 'GET', path: '/registry/service-summary', handle: () => query.serviceSummary({ channelName: 'profile' }) },
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
