import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  type ZLinkLocationRuntimeQuery,
  type ZLinkChannelClient
} from '@zlink-systems/framework';
import type { ProfileRes, ProfileReq } from '../../../Shared/messages';
import { ChannelNames, PacketNames } from '../../../Shared/messages';
import type { HttpRoute } from '../Support/http-server';

export function createConsumerEndpoints(
  channel: ZLinkChannelClient,
  locationQuery: ZLinkLocationRuntimeQuery,
  stop: () => void
): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'consumer' }) },
    { method: 'POST', path: '/profile/request', handle: (body) => requestProfile(channel, body as ProfileReq) },
    { method: 'POST', path: '/profile/request-once', handle: (body) => requestProfileOnce(channel, body as ProfileReq) },
    { method: 'GET', path: '/location/status', handle: () => locationQuery.getStatus() },
    {
      method: 'GET',
      path: '/location/peers',
      handle: async () => {
        const rows = await locationQuery.listPeerLocations({
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

async function requestProfile(channel: ZLinkChannelClient, request: ProfileReq): Promise<ProfileRes> {
  return await channel
    .requestToChannel(ChannelNames.profile, request)
    .packetName(PacketNames.profileReq)
    .timeout(5000)
    .submit<ProfileRes>();
}

async function requestProfileOnce(channel: ZLinkChannelClient, request: ProfileReq): Promise<ProfileRes> {
  return await channel
    .requestToChannel(ChannelNames.profile, request)
    .packetName(PacketNames.profileReq)
    .timeout(1000)
    .submit<ProfileRes>();
}
