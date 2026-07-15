import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  type ZLinkLocationRuntimeQuery,
  type ZLinkChannelClient
} from '@zlink-systems/framework';
import { ProfileReq, type ProfileRes } from '../../../Shared/messages';
import { ChannelNames } from '../../../Shared/messages';
import type { HttpRoute } from '../Support/http-server';

export function createConsumerEndpoints(
  channel: ZLinkChannelClient,
  locationQuery: ZLinkLocationRuntimeQuery,
  stop: () => void
): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'consumer' }) },
    { method: 'POST', path: '/profile/request', handle: (body) => requestProfile(channel, toProfileReq(body)) },
    { method: 'POST', path: '/profile/request-once', handle: (body) => requestProfileOnce(channel, toProfileReq(body)) },
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
          ownerId: row.ownerId,
          draining: row.draining ?? false
        }));
      }
    },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

function toProfileReq(body: unknown): ProfileReq {
  const request = body as ProfileReq;
  return new ProfileReq(request.value, request.marker);
}

async function requestProfile(channel: ZLinkChannelClient, request: ProfileReq): Promise<ProfileRes> {
  return await channel
    .requestToChannel(ChannelNames.profile, request)
    .timeout(5000)
    .submit<ProfileRes>();
}

async function requestProfileOnce(channel: ZLinkChannelClient, request: ProfileReq): Promise<ProfileRes> {
  return await channel
    .requestToChannel(ChannelNames.profile, request)
    .timeout(1000)
    .submit<ProfileRes>();
}
