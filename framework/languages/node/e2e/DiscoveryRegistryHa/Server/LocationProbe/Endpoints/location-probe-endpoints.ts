import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  ZLinkLocationTopologyState,
  type ZLinkLocationStore
} from '@zlink-systems/framework';
import type { LocationProbeOptions } from '../Configuration/location-probe-options';
import type { HttpRoute } from '../Support/http-server';

export function createLocationProbeEndpoints(
  options: LocationProbeOptions,
  store: ZLinkLocationStore,
  stop: () => void
): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'location-store-probe', rid: options.rid }) },
    { method: 'GET', path: '/location/status', handle: () => ({ storeHealthy: true, watchEnabled: false }) },
    {
      method: 'GET',
      path: '/location/service-summary',
      handle: async () => {
        const rows = await store.listPeers({
          autoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
          meshName: 'profile',
          role: ZLinkLocationRole.Router
        });
        return [{ channelName: 'profile', serviceRole: ZLinkLocationRole.Router, totalCount: rows.length, readyCount: rows.length }];
      }
    },
    {
      method: 'GET',
      path: '/location/topology',
      handle: async () => {
        const rows = await store.listPeers({
          autoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
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
    {
      method: 'GET',
      path: '/location/member-peers',
      handle: async () => {
        const rows = await store.listPeers({
          autoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
          meshName: 'profile',
          role: ZLinkLocationRole.Router
        });
        return rows.map((row) => ({ rid: row.nodeRid, endpoint: row.endpoint, state: ZLinkLocationTopologyState.Ready }));
      }
    },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}
