// SF-C2: graceful shutdown 대조 (drain 뒤 owner 정리) 시나리오를 검증한다.
import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

interface PeerDto {
  readonly endpoint: string;
  readonly nodeRid?: string;
}

export async function runSfC2(options: ClientOptions): Promise<void> {
  await waitForMissingPeer(options.consumerUrl, 'api-b');
  await waitForProviderReply(options.consumerUrl, 'api-a', 'sf-c2-stable-after-shutdown');

  for (let i = 0; i < 4; i++) {
    const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', { value: `sf-c2-after-${i}` });
    ensure(reply.value === `profile:sf-c2-after-${i}`, `SF-C2 follow-up request ${i} value mismatch.`);
    ensure(reply.providerRid === 'api-a', `SF-C2 follow-up request ${i} was served by '${reply.providerRid}'.`);
  }

  const evidence = await getJson<string[]>(options.providerAUrl, '/evidence');
  ensure(evidence.some((entry) => entry.includes('value=sf-c2-after-')), 'SF-C2 api-a evidence missing.');
  console.log('scenario SF-C2 passed');
}

async function waitForMissingPeer(baseUrl: string, rid: string): Promise<void> {
  const deadline = Date.now() + 5000;
  let last: readonly PeerDto[] = [];
  while (Date.now() < deadline) {
    last = await getJson<PeerDto[]>(baseUrl, '/location/peers');
    if (!last.some((peer) => peer.nodeRid === rid)) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`SF-C2 expected peer ${rid} to be absent after graceful shutdown, last=${JSON.stringify(last)}`);
}

async function waitForProviderReply(baseUrl: string, rid: string, prefix: string): Promise<void> {
  const deadline = Date.now() + 10000;
  let index = 0;
  let consecutive = 0;
  while (Date.now() < deadline) {
    const value = `${prefix}-${index++}`;
    try {
      const reply = await postJson<ProfileRes>(baseUrl, '/profile/request', { value });
      ensure(reply.value === `profile:${value}`, `SF-C2 reply value mismatch for ${value}.`);
      if (reply.providerRid === rid) {
        consecutive += 1;
        if (consecutive >= 3) {
          return;
        }
      } else {
        consecutive = 0;
      }
    } catch {
      // Wait for the routing table to converge after graceful provider shutdown.
      consecutive = 0;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`SF-C2 expected request routing to reach ${rid}.`);
}
