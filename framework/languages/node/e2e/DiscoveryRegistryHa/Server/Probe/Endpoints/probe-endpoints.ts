import {
  ZLinkServiceRole,
  ZLinkTopologyState,
  type ZLinkRegistryQueryClient
} from '@zlink-systems/framework';
import type { ProbeOptions } from '../Configuration/probe-options';
import type { HttpRoute } from '../../Registry/Support/http-server';

export function createProbeEndpoints(
  options: ProbeOptions,
  query: ZLinkRegistryQueryClient,
  stop: () => void
): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', registryRouterEndpoint: options.registryRouterEndpoint }) },
    {
      method: 'GET',
      path: '/registry/topology',
      handle: () => query.topology({
        channelName: 'profile',
        serviceRole: ZLinkServiceRole.Router,
        state: ZLinkTopologyState.Ready
      })
    },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}
