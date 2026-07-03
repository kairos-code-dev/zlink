import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson, postJsonWithin } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

interface PeerDto {
  readonly endpoint: string;
  readonly nodeRid?: string;
}

export async function runSfD2(options: ClientOptions): Promise<void> {
  const replies = await driveTolerantRequests(options.consumerUrl, 10000);
  ensure(replies.some((reply) => reply.providerRid === 'api-a'), 'SF-D2 no request was served by surviving api-a.');

  await waitForPeer(options.consumerUrl, 'api-a');
  await waitForMissingPeer(options.consumerUrl, 'api-b');

  for (let i = 0; i < 8; i++) {
    const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', { value: `sf-d2-after-${i}` });
    ensure(reply.value === `profile:sf-d2-after-${i}`, `SF-D2 post-recovery request ${i} value mismatch.`);
    ensure(reply.providerRid === 'api-a', `SF-D2 post-recovery request ${i} was served by '${reply.providerRid}'.`);
  }

  console.log('scenario SF-D2 passed');
}

async function driveTolerantRequests(baseUrl: string, windowMs: number): Promise<ProfileRes[]> {
  const replies: ProfileRes[] = [];
  const deadline = Date.now() + windowMs;
  let lastSuccess = Date.now();
  let maxGap = 0;
  let index = 0;
  while (Date.now() < deadline) {
    try {
      const reply = await postJsonWithin<ProfileRes>(baseUrl, '/profile/request-once', { value: `sf-d2-${index++}` }, 1500);
      ensure(reply.value.startsWith('profile:sf-d2-'), 'SF-D2 request returned an unexpected value.');
      replies.push(reply);
      const now = Date.now();
      maxGap = Math.max(maxGap, now - lastSuccess);
      lastSuccess = now;
    } catch {
      // A request can land on the provider killed during the long outage.
    }
    await new Promise((resolve) => setTimeout(resolve, 150));
  }

  maxGap = Math.max(maxGap, Date.now() - lastSuccess);
  ensure(replies.length > 0, 'SF-D2 request window produced no successful traffic.');
  ensure(maxGap < 6000, `SF-D2 successful traffic stalled for ${maxGap}ms.`);
  return replies;
}

async function waitForPeer(baseUrl: string, rid: string): Promise<void> {
  const deadline = Date.now() + 10000;
  let last: readonly PeerDto[] = [];
  while (Date.now() < deadline) {
    last = await getJson<PeerDto[]>(baseUrl, '/location/peers');
    if (last.some((peer) => peerHasRid(peer, rid))) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`SF-D2 expected live peer ${rid}, last=${JSON.stringify(last)}`);
}

async function waitForMissingPeer(baseUrl: string, rid: string): Promise<void> {
  const deadline = Date.now() + 10000;
  let last: readonly PeerDto[] = [];
  while (Date.now() < deadline) {
    last = await getJson<PeerDto[]>(baseUrl, '/location/peers');
    if (!last.some((peer) => peerHasRid(peer, rid))) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`SF-D2 expected peer ${rid} to be absent, last=${JSON.stringify(last)}`);
}

function peerHasRid(peer: PeerDto, rid: string): boolean {
  return peer.nodeRid === rid;
}
