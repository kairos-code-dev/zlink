import type { ZLinkChannelClient, ZLinkRouteClient } from '@zlink-systems/framework';
import type {
  EvidenceWaitRequest,
  ProfileCommand,
  ProfileReply,
  ProfileRequest,
  RouteMissingResult,
  ScenarioRoutePing,
  ScenarioRoutePong
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
      handle: (body) => requestProfileWithRetry(channel, 'profile', body as ProfileRequest)
    },
    {
      method: 'POST',
      path: '/profile/manual',
      handle: (body) => requestProfileWithRetry(channel, 'profile.manual', body as ProfileRequest)
    },
    {
      method: 'POST',
      path: '/profile/command',
      handle: async (body) => {
        await sendProfileWithRetry(channel, 'profile', body as ProfileCommand);
        return { status: 'sent' };
      }
    },
    {
      method: 'POST',
      path: '/profile/route/request',
      handle: (body) => requestRouteWithRetry(route, 'api-b', body as ScenarioRoutePing)
    },
    {
      method: 'POST',
      path: '/profile/route/missing',
      handle: async (body): Promise<RouteMissingResult> => {
        let failed = false;
        try {
          await route
            .request('profile.route', 'missing-rid', body as ScenarioRoutePing)
            .packetName(PacketNames.scenarioRoutePing)
            .timeout(300)
            .submit<ScenarioRoutePong>();
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
        const request = body as EvidenceWaitRequest;
        const timeout = Math.max(1, Math.min(request.timeoutMilliseconds ?? 10000, 30000));
        return evidence.waitUntil((line) => line.includes(request.contains), timeout);
      }
    },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

async function requestProfileWithRetry(
  channel: ZLinkChannelClient,
  channelName: string,
  request: ProfileRequest
): Promise<ProfileReply> {
  return retryUntil(async () => channel
    .requestToChannel(channelName, request)
    .packetName(PacketNames.profileRequest)
    .timeout(5000)
    .submit<ProfileReply>(), 'profile request channel route');
}

async function sendProfileWithRetry(
  channel: ZLinkChannelClient,
  channelName: string,
  command: ProfileCommand
): Promise<void> {
  await retryUntil(async () => channel
    .sendToChannel(channelName, command)
    .packetName(PacketNames.profileCommand)
    .submit(), 'profile send channel route');
}

async function requestRouteWithRetry(
  route: ZLinkRouteClient,
  targetRid: string,
  request: ScenarioRoutePing
): Promise<ScenarioRoutePong> {
  return retryUntil(async () => route
    .request('profile.route', targetRid, request)
    .packetName(PacketNames.scenarioRoutePing)
    .timeout(5000)
    .submit<ScenarioRoutePong>(), 'route mesh target');
}

async function retryUntil<T>(operation: () => Promise<T>, label: string): Promise<T> {
  const deadline = Date.now() + 30000;
  let last: unknown;
  while (Date.now() < deadline) {
    try {
      return await operation();
    } catch (error) {
      last = error;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
  }
  throw new Error(`Timed out waiting for ${label}: ${last instanceof Error ? last.message : String(last)}`);
}
