import type { ZLinkChannelClient, ZLinkRouteClient } from '@zlink-systems/framework';
import {
  ProfileMsg,
  ProfileReq,
  ScenarioRouteReq,
  type EvidenceWaitReq,
  type ProfileRes,
  type RouteMissingRes,
  type ScenarioRouteRes
} from '../../../Shared/messages';
import type { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';

export function createProviderEndpoints(
  evidence: EvidenceStore,
  channel: ZLinkChannelClient,
  route: ZLinkRouteClient,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'provider', rid: evidence.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/profile/request',
      handle: (body) => requestProfile(channel, 'profile', new ProfileReq((body as ProfileReq).value))
    },
    {
      method: 'POST',
      path: '/profile/manual',
      handle: (body) => requestProfile(channel, 'profile.manual', new ProfileReq((body as ProfileReq).value))
    },
    {
      method: 'POST',
      path: '/profile/command',
      handle: async (body) => {
        await sendProfile(channel, 'profile', new ProfileMsg((body as ProfileMsg).commandId));
        return { status: 'sent' };
      }
    },
    {
      method: 'POST',
      path: '/profile/route/request',
      handle: (body) => requestRoute(route, 'api-b', new ScenarioRouteReq((body as ScenarioRouteReq).value))
    },
    {
      method: 'POST',
      path: '/profile/route/missing',
      handle: async (body): Promise<RouteMissingRes> => {
        let failed = false;
        try {
          await route
            .requestToNode('profile.route', 'missing-rid', new ScenarioRouteReq((body as ScenarioRouteReq).value))
            .timeout(300)
            .submit<ScenarioRouteRes>();
        } catch {
          failed = true;
        }
        return { failed };
      }
    },
    { method: 'POST', path: '/evidence/clear', handle: () => { evidence.clear(); return { status: 'cleared' }; } },
    {
      method: 'POST',
      path: '/evidence/wait',
      handle: (body) => {
        const request = body as EvidenceWaitReq;
        const timeout = Math.max(1, Math.min(request.timeoutMilliseconds ?? 10000, 30000));
        return evidence.waitUntil((entries) => entries.some((line) => line.includes(request.contains)), timeout);
      }
    },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

async function requestProfile(
  channel: ZLinkChannelClient,
  channelName: string,
  request: ProfileReq
): Promise<ProfileRes> {
  return channel
    .requestToChannel(channelName, request)
    .timeout(5000)
    .submit<ProfileRes>();
}

async function sendProfile(
  channel: ZLinkChannelClient,
  channelName: string,
  command: ProfileMsg
): Promise<void> {
  await channel
    .sendToChannel(channelName, command)
    .submit();
}

async function requestRoute(
  route: ZLinkRouteClient,
  targetRid: string,
  request: ScenarioRouteReq
): Promise<ScenarioRouteRes> {
  return route
    .requestToNode('profile.route', targetRid, request)
    .timeout(5000)
    .submit<ScenarioRouteRes>();
}
