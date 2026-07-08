import type { ZLinkChannelClient, ZLinkRouteClient } from '@zlink-systems/framework';
import type {
  EvidenceWaitReq,
  ProfileMsg,
  ProfileRes,
  ProfileReq,
  RouteMissingRes,
  ScenarioRouteReq,
  ScenarioRouteRes
} from '../../../Shared/messages';
import { PacketNames } from '../../../Shared/messages';
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
      handle: (body) => requestProfile(channel, 'profile', body as ProfileReq)
    },
    {
      method: 'POST',
      path: '/profile/manual',
      handle: (body) => requestProfile(channel, 'profile.manual', body as ProfileReq)
    },
    {
      method: 'POST',
      path: '/profile/command',
      handle: async (body) => {
        await sendProfile(channel, 'profile', body as ProfileMsg);
        return { status: 'sent' };
      }
    },
    {
      method: 'POST',
      path: '/profile/route/request',
      handle: (body) => requestRoute(route, 'api-b', body as ScenarioRouteReq)
    },
    {
      method: 'POST',
      path: '/profile/route/missing',
      handle: async (body): Promise<RouteMissingRes> => {
        let failed = false;
        try {
          await route
            .requestToNode('profile.route', 'missing-rid', body as ScenarioRouteReq)
            .packetName(PacketNames.scenarioRouteReq)
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
    .packetName(PacketNames.profileReq)
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
    .packetName(PacketNames.profileMsg)
    .submit();
}

async function requestRoute(
  route: ZLinkRouteClient,
  targetRid: string,
  request: ScenarioRouteReq
): Promise<ScenarioRouteRes> {
  return route
    .requestToNode('profile.route', targetRid, request)
    .packetName(PacketNames.scenarioRouteReq)
    .timeout(5000)
    .submit<ScenarioRouteRes>();
}
