import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type {
  PayloadRes,
  PayloadReq,
  ProfileMsg,
  ProfileRes,
  ProfileReq,
  RequestFailureRes
} from '../../../Shared/messages';
import { PacketNames } from '../../../Shared/messages';
import type { HttpRoute } from '../Support/http-server';

export function createConsumerEndpoints(channel: ZLinkChannelClient, stop: () => void): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready' }) },
    { method: 'POST', path: '/profile/batch-request', handle: (body) => batchRequest(channel, body as ProfileReq[]) },
    { method: 'POST', path: '/profile/request', handle: (body) => requestProfile(channel, body as ProfileReq, 5000) },
    { method: 'POST', path: '/profile/slow-request', handle: (body) => requestProfileFailure(channel, body as ProfileReq, 100) },
    { method: 'POST', path: '/profile/missing-request', handle: (body) => requestMissingProfile(channel, body as ProfileReq) },
    {
      method: 'POST',
      path: '/profile/missing-command',
      handle: async (body) => {
        channel
          .sendToChannel('profile', body as ProfileMsg)
          .packetName(PacketNames.missingProfileMsg)
          .submit();
        return { status: 'sent' };
      }
    },
    { method: 'POST', path: '/profile/payload', handle: (body) => requestPayload(channel, body as PayloadReq) },
    { method: 'POST', path: '/profile/payload-over-limit', handle: (body) => requestPayloadFailure(channel, body as PayloadReq) },
    { method: 'POST', path: '/profile/backpressure/reset', handle: () => ({ status: 'ready' }) },
    { method: 'POST', path: '/profile/backpressure/send', handle: (body) => submitProfileUnderPressure(channel, body as ProfileMsg) },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

async function batchRequest(channel: ZLinkChannelClient, requests: readonly ProfileReq[]): Promise<ProfileRes[]> {
  const replies: ProfileRes[] = [];
  for (const request of requests) {
    replies.push(await requestProfile(channel, request, 5000));
  }
  return replies;
}

async function requestProfile(
  channel: ZLinkChannelClient,
  request: ProfileReq,
  timeoutMs: number
): Promise<ProfileRes> {
  return channel
    .requestToChannel('profile', request)
    .packetName(PacketNames.profileReq)
    .timeout(timeoutMs)
    .submit<ProfileRes>();
}

async function requestPayload(channel: ZLinkChannelClient, request: PayloadReq): Promise<PayloadRes> {
  return channel
    .requestToChannel('profile', request)
    .packetName(PacketNames.payloadReq)
    .timeout(10000)
    .submit<PayloadRes>();
}

async function requestPayloadFailure(channel: ZLinkChannelClient, request: PayloadReq): Promise<RequestFailureRes> {
  try {
    await channel
      .requestToChannel('profile', request)
      .packetName(PacketNames.payloadReq)
      .timeout(5000)
      .submit<PayloadRes>();
    return { failed: false, failureType: '' };
  } catch (error) {
    return { failed: true, failureType: error instanceof Error ? error.name : 'Error' };
  }
}

async function requestProfileFailure(
  channel: ZLinkChannelClient,
  request: ProfileReq,
  timeoutMs: number
): Promise<RequestFailureRes> {
  try {
    await requestProfile(channel, request, timeoutMs);
    return { failed: false, failureType: '' };
  } catch (error) {
    return { failed: true, failureType: error instanceof Error ? error.name : 'Error' };
  }
}

async function requestMissingProfile(channel: ZLinkChannelClient, request: ProfileReq): Promise<RequestFailureRes> {
  try {
    await channel
      .requestToChannel('profile', request)
      .packetName(PacketNames.missingProfileReq)
      .timeout(5000)
      .submit<ProfileRes>();
    return { failed: false, failureType: '' };
  } catch (error) {
    return { failed: true, failureType: error instanceof Error ? error.name : 'Error' };
  }
}

function submitProfileUnderPressure(channel: ZLinkChannelClient, command: ProfileMsg): string {
  channel
    .sendToChannel('profile', command)
    .packetName(PacketNames.profileMsg)
    .submit();
  return 'Submitted';
}
