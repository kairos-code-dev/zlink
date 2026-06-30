import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type { ProfileReply, ProfileRequest } from '../../../Shared/messages';
import { ChannelNames, PacketNames } from '../../../Shared/messages';
import type { HttpRoute } from '../Support/http-server';

export function createConsumerEndpoints(channel: ZLinkChannelClient, stop: () => void): readonly HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'consumer' }) },
    { method: 'POST', path: '/profile/request', handle: (body) => requestProfileWithRetry(channel, body as ProfileRequest) },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

async function requestProfileWithRetry(channel: ZLinkChannelClient, request: ProfileRequest): Promise<ProfileReply> {
  return retryUntil(async () => channel
    .requestToChannel(ChannelNames.profile, request)
    .packetName(PacketNames.profileRequest)
    .timeout(5000)
    .submit<ProfileReply>(), 'discovered profile provider');
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
