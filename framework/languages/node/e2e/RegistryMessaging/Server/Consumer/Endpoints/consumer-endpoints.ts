import {
  ZLinkFrameworkException,
  type ZLinkChannelClient
} from '@zlink-systems/framework';
import {
  MissingProfileMsg,
  MissingProfileReq,
  PayloadReq,
  ProfileMsg,
  ProfileReq,
  type PayloadRes,
  type ProfileRes,
  type RequestFailureRes
} from '../../../Shared/messages';
import type { HttpRoute } from '../Support/http-server';

export function createConsumerEndpoints(channel: ZLinkChannelClient, stop: () => void): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready' }) },
    { method: 'POST', path: '/profile/batch-request', handle: (body) => batchRequest(channel, (body as ProfileReq[]).map((request) => new ProfileReq(request.value))) },
    { method: 'POST', path: '/profile/request', handle: (body) => requestProfile(channel, new ProfileReq((body as ProfileReq).value), 5000) },
    { method: 'POST', path: '/profile/slow-request', handle: (body) => requestProfileFailure(channel, new ProfileReq((body as ProfileReq).value), 100) },
    { method: 'POST', path: '/profile/missing-request', handle: (body) => requestMissingProfile(channel, new MissingProfileReq((body as ProfileReq).value)) },
    {
      method: 'POST',
      path: '/profile/missing-command',
      handle: async (body) => {
        channel
          .sendToChannel('profile', new MissingProfileMsg((body as ProfileMsg).commandId))
          .submit();
        return { status: 'sent' };
      }
    },
    { method: 'POST', path: '/profile/payload', handle: (body) => requestPayload(channel, toPayloadReq(body)) },
    { method: 'POST', path: '/profile/payload-over-limit', handle: (body) => requestPayloadFailure(channel, toPayloadReq(body)) },
    { method: 'POST', path: '/profile/backpressure/reset', handle: () => ({ status: 'ready' }) },
    { method: 'POST', path: '/profile/backpressure/send', handle: (body) => submitProfileUnderPressure(channel, new ProfileMsg((body as ProfileMsg).commandId)) },
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
    .timeout(timeoutMs)
    .submit<ProfileRes>();
}

async function requestPayload(channel: ZLinkChannelClient, request: PayloadReq): Promise<PayloadRes> {
  return channel
    .requestToChannel('profile', request)
    .timeout(10000)
    .submit<PayloadRes>();
}

async function requestPayloadFailure(channel: ZLinkChannelClient, request: PayloadReq): Promise<RequestFailureRes> {
  try {
    await channel
      .requestToChannel('profile', request)
      .timeout(5000)
      .submit<PayloadRes>();
    return { failed: false, failureType: '' };
  } catch (error) {
    return { failed: true, failureType: publicFailureType(error) };
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
    return { failed: true, failureType: publicFailureType(error) };
  }
}

async function requestMissingProfile(channel: ZLinkChannelClient, request: MissingProfileReq): Promise<RequestFailureRes> {
  try {
    await channel
      .requestToChannel('profile', request)
      .timeout(5000)
      .submit<ProfileRes>();
    return { failed: false, failureType: '' };
  } catch (error) {
    return { failed: true, failureType: publicFailureType(error) };
  }
}

function publicFailureType(error: unknown): string {
  return error instanceof ZLinkFrameworkException ? error.kind
    : error instanceof Error ? error.name
      : 'Error';
}

function toPayloadReq(body: unknown): PayloadReq {
  const request = body as PayloadReq;
  return new PayloadReq(request.marker, request.payload);
}

function submitProfileUnderPressure(channel: ZLinkChannelClient, command: ProfileMsg): string {
  channel
    .sendToChannel('profile', command)
    .submit();
  return 'Submitted';
}
