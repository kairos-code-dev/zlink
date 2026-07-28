import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkPeerState,
  type ZLinkRouteClient,
  type ZLinkRouteMeshRuntime
} from '@zlink-systems/framework';
import { ScenarioRouteReq, type ScenarioRouteRes } from '../../../Shared/messages';
import type { HttpRoute } from '../../Provider/Support/http-server';

const meshName = 'registry.messaging.rm-a3';

export function createObjectClientEndpoints(
  rid: string,
  runtime: ZLinkRouteMeshRuntime,
  route: ZLinkRouteClient,
  stop: () => void
): HttpRoute[] {
  return [
    {
      method: 'GET',
      path: '/health',
      handle: () => ({ status: 'ready', role: 'object-client', rid })
    },
    {
      method: 'GET',
      path: '/rm-a3/status',
      handle: () => {
        const snapshot = runtime.snapshot(meshName);
        return {
          rid: String(snapshot.rid),
          state: snapshot.state,
          readyPeerCount: snapshot.peers.filter((peer) => peer.ready).length,
          peers: snapshot.peers.map((peer) => ({
            rid: String(peer.rid),
            state: peerStateName(peer.state),
            ready: peer.ready,
            lastFailure: peer.lastFailure
          }))
        };
      }
    },
    {
      method: 'POST',
      path: '/rm-a3/node-direct',
      handle: async (body) => {
        const targetRid = String((body as { targetRid?: unknown }).targetRid ?? '');
        try {
          await route
            .requestToNode(meshName, targetRid, new ScenarioRouteReq('rm-a3'))
            .timeout(500)
            .submit<ScenarioRouteRes>();
          return { terminal: 'UnexpectedSuccess', errorKind: '' };
        } catch (error) {
          const kind = error instanceof ZLinkFrameworkException ? error.kind : 'Error';
          return {
            terminal: kind === ZLinkFrameworkErrorKind.RequestTargetNotFound
              ? 'NotFound'
              : 'Failed',
            errorKind: kind
          };
        }
      }
    },
    {
      method: 'POST',
      path: '/shutdown',
      handle: () => {
        stop();
        return { status: 'stopping' };
      }
    }
  ];
}

function peerStateName(state: ZLinkPeerState): string {
  switch (state) {
    case ZLinkPeerState.Connecting: return 'connecting';
    case ZLinkPeerState.Ready: return 'ready';
    case ZLinkPeerState.Draining: return 'draining';
    case ZLinkPeerState.NotConnected: return 'not_connected';
    case ZLinkPeerState.NotRequired: return 'not_required';
  }
}
