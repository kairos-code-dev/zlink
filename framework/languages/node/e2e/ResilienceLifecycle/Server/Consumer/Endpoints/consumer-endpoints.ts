import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type {
  PayloadReply,
  PayloadRequest,
  ProfileCommand,
  ProfileReply,
  ProfileRequest,
  RequestFailureResult,
  TimeoutRequestResult
} from '../../../Shared/messages';
import { PacketNames } from '../../../Shared/messages';
import type { HttpRoute } from '../Support/http-server';

export function createConsumerEndpoints(
  channel: ZLinkChannelClient,
  requestWithNewClient: (request: ProfileRequest) => Promise<ProfileReply>,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready' }) },
    { method: 'POST', path: '/profile/batch-request', handle: (body) => batchRequest(channel, body as ProfileRequest[]) },
    { method: 'POST', path: '/profile/request', handle: (body) => requestProfileWithRetry(channel, body as ProfileRequest, 5000) },
    { method: 'POST', path: '/profile/request/no-retry', handle: (body) => requestProfileOnce(channel, body as ProfileRequest, 10000) },
    { method: 'POST', path: '/profile/request/timeout/100', handle: (body) => requestProfileTimeout(channel, body as ProfileRequest, 100) },
    { method: 'POST', path: '/profile/request/timeout/10000', handle: (body) => requestProfileTimeout(channel, body as ProfileRequest, 10000) },
    { method: 'POST', path: '/profile/request/new-client', handle: (body) => requestWithNewClient(body as ProfileRequest) },
    { method: 'POST', path: '/profile/command', handle: (body) => sendProfile(channel, body as ProfileCommand) },
    { method: 'POST', path: '/profile/slow-request', handle: (body) => requestProfileFailure(channel, body as ProfileRequest, 100) },
    { method: 'POST', path: '/profile/missing-request', handle: (body) => requestMissingProfile(channel, body as ProfileRequest) },
    {
      method: 'POST',
      path: '/profile/missing-command',
      handle: async (body) => {
        await channel
          .sendToChannel('profile', body as ProfileCommand)
          .packetName(PacketNames.missingProfileCommand)
          .submit();
        return { status: 'sent' };
      }
    },
    { method: 'POST', path: '/profile/payload', handle: (body) => requestPayloadWithRetry(channel, body as PayloadRequest) },
    { method: 'POST', path: '/profile/backpressure/reset', handle: () => ({ status: 'ready' }) },
    { method: 'POST', path: '/profile/backpressure/send', handle: (body) => sendProfileWithBoundedFailure(channel, body as ProfileCommand) },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

async function batchRequest(channel: ZLinkChannelClient, requests: readonly ProfileRequest[]): Promise<ProfileReply[]> {
  const replies: ProfileReply[] = [];
  for (const request of requests) {
    replies.push(await requestProfileWithRetry(channel, request, 5000));
  }
  return replies;
}

export async function requestProfileWithRetry(
  channel: ZLinkChannelClient,
  request: ProfileRequest,
  timeoutMs: number
): Promise<ProfileReply> {
  return retryUntil(async () => channel
    .requestToChannel('profile', request)
    .packetName(PacketNames.profileRequest)
    .timeout(timeoutMs)
    .submit<ProfileReply>(), 'direct profile endpoints');
}

async function requestProfileOnce(
  channel: ZLinkChannelClient,
  request: ProfileRequest,
  timeoutMs: number
): Promise<ProfileReply> {
  return await channel
    .requestToChannel('profile', request)
    .packetName(PacketNames.profileRequest)
    .timeout(timeoutMs)
    .submit<ProfileReply>();
}

async function requestProfileTimeout(
  channel: ZLinkChannelClient,
  request: ProfileRequest,
  timeoutMs: number
): Promise<TimeoutRequestResult> {
  try {
    await channel
      .requestToChannel('profile', request)
      .packetName(PacketNames.profileRequest)
      .timeout(timeoutMs)
      .submit<ProfileReply>();
    return { status: 200, timedOut: false };
  } catch {
    return { status: 408, timedOut: true };
  }
}

async function requestPayloadWithRetry(channel: ZLinkChannelClient, request: PayloadRequest): Promise<PayloadReply> {
  return retryUntil(async () => channel
    .requestToChannel('profile', request)
    .packetName(PacketNames.payloadRequest)
    .timeout(10000)
    .submit<PayloadReply>(), 'payload profile endpoint');
}

async function sendProfile(channel: ZLinkChannelClient, command: ProfileCommand): Promise<{ readonly status: string }> {
  await channel
    .sendToChannel('profile', command)
    .packetName(PacketNames.profileCommand)
    .submit();
  return { status: 'sent' };
}

async function requestProfileFailure(
  channel: ZLinkChannelClient,
  request: ProfileRequest,
  timeoutMs: number
): Promise<RequestFailureResult> {
  try {
    await requestProfileWithRetry(channel, request, timeoutMs);
    return { failed: false, failureType: '' };
  } catch (error) {
    return { failed: true, failureType: error instanceof Error ? error.name : 'Error' };
  }
}

async function requestMissingProfile(channel: ZLinkChannelClient, request: ProfileRequest): Promise<RequestFailureResult> {
  try {
    await channel
      .requestToChannel('profile', request)
      .packetName(PacketNames.missingProfileRequest)
      .timeout(5000)
      .submit<ProfileReply>();
    return { failed: false, failureType: '' };
  } catch (error) {
    return { failed: true, failureType: error instanceof Error ? error.name : 'Error' };
  }
}

async function sendProfileWithBoundedFailure(channel: ZLinkChannelClient, command: ProfileCommand): Promise<string> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 500);
  try {
    const send = channel
      .sendToChannel('profile', command)
      .packetName(PacketNames.profileCommand)
      .submit(controller.signal);
    const timeout = new Promise<string>((resolve) => setTimeout(() => resolve('BoundedFailure'), 750));
    const result = await Promise.race([send.then(() => 'Accepted'), timeout]);
    return result;
  } catch {
    return 'BoundedFailure';
  } finally {
    clearTimeout(timer);
  }
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
