import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  type IZLinkLocationRuntimeQuery,
  type ZLinkChannelClient
} from '@zlink-systems/framework';
import type { ProfileRes, ProfileReq } from '../../../Shared/messages';
import { ChannelNames, PacketNames } from '../../../Shared/messages';
import type { HttpRoute } from '../Support/http-server';

export function createConsumerEndpoints(
  channel: ZLinkChannelClient,
  locationQuery: IZLinkLocationRuntimeQuery,
  stop: () => void
): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'consumer' }) },
    { method: 'POST', path: '/profile/request', handle: (body) => requestProfileWithRetry(channel, body as ProfileReq) },
    { method: 'POST', path: '/profile/request-once', handle: (body) => requestProfileOnce(channel, body as ProfileReq) },
    { method: 'GET', path: '/location/status', handle: () => locationQuery.getStatus() },
    {
      method: 'GET',
      path: '/location/peers',
      handle: async () => {
        const rows = await locationQuery.listPeers({
          autoConnectType: ZLinkLocationAutoConnectType.ClientServer,
          meshName: ChannelNames.profile,
          role: ZLinkLocationRole.Router
        });
        return rows.map((row) => ({
          endpoint: row.endpoint,
          nodeRid: String(row.nodeRid),
          ownerId: row.ownerId
        }));
      }
    },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

async function requestProfileWithRetry(channel: ZLinkChannelClient, request: ProfileReq): Promise<ProfileRes> {
  return retryUntil(async () => channel
    .requestToChannel(ChannelNames.profile, request)
    .packetName(PacketNames.profileReq)
    .timeout(5000)
    .submit<ProfileRes>(), 'discovered profile provider');
}

async function requestProfileOnce(channel: ZLinkChannelClient, request: ProfileReq): Promise<ProfileRes> {
  return await channel
    .requestToChannel(ChannelNames.profile, request)
    .packetName(PacketNames.profileReq)
    .timeout(1000)
    .submit<ProfileRes>();
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
