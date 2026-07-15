// SF-A2: polling fallback (watch 없는 store) 시나리오를 검증한다.
import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

interface LocationRuntimeStatusDto {
  readonly storeHealthy: boolean;
  readonly watchEnabled: boolean;
  readonly pollingIntervalMs: number;
  readonly ownerLeaseHealthy: boolean;
}

interface PeerDto {
  readonly endpoint: string;
  readonly nodeRid?: string;
}

export async function runSfA2(options: ClientOptions): Promise<void> {
  const status = await getJson<LocationRuntimeStatusDto>(options.consumerUrl, '/location/status');
  ensure(status.storeHealthy && status.ownerLeaseHealthy, 'SF-A2 expected healthy location runtime status.');
  ensure(status.watchEnabled === false, 'SF-A2 expected Redis location store to run without watch support.');
  ensure(status.pollingIntervalMs > 0, 'SF-A2 expected polling interval to be exposed.');

  await waitForPeerCount(options.consumerUrl, 1, 'initial single provider');
  console.log('scenario-control SF-A2 start-provider-b');
  await waitForPeer(options.consumerUrl, 'api-b');
  await waitForProviderReply(options.consumerUrl, 'api-b', 'sf-a2-added');
  console.log('scenario-control SF-A2 shutdown-provider-b');
  await waitForMissingPeer(options.consumerUrl, 'api-b');
  await waitForProviderReply(options.consumerUrl, 'api-a', 'sf-a2-stable-after-remove', 3);

  for (let i = 0; i < 4; i++) {
    const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', { value: `sf-a2-after-remove-${i}` });
    ensure(reply.value === `profile:sf-a2-after-remove-${i}`, `SF-A2 removal request ${i} value mismatch.`);
    ensure(reply.providerRid === 'api-a', `SF-A2 removal request ${i} was served by '${reply.providerRid}'.`);
  }

  console.log('scenario SF-A2 passed');
}

async function waitForPeerCount(baseUrl: string, count: number, label: string): Promise<void> {
  const deadline = Date.now() + 10000;
  let last: readonly PeerDto[] = [];
  while (Date.now() < deadline) {
    last = await getJson<PeerDto[]>(baseUrl, '/location/peers');
    if (last.length === count) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`SF-A2 expected ${label} peer count ${count}, last=${JSON.stringify(last)}`);
}

async function waitForPeer(baseUrl: string, rid: string): Promise<void> {
  const deadline = Date.now() + 10000;
  let last: readonly PeerDto[] = [];
  while (Date.now() < deadline) {
    last = await getJson<PeerDto[]>(baseUrl, '/location/peers');
    if (last.some((peer) => peer.nodeRid === rid)) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`SF-A2 expected peer ${rid}, last=${JSON.stringify(last)}`);
}

async function waitForProviderReply(
  baseUrl: string,
  rid: string,
  prefix: string,
  requiredConsecutive = 1
): Promise<void> {
  const deadline = Date.now() + 10000;
  let index = 0;
  let consecutive = 0;
  while (Date.now() < deadline) {
    const value = `${prefix}-${index++}`;
    try {
      const reply = await postJson<ProfileRes>(baseUrl, '/profile/request', { value });
      ensure(reply.value === `profile:${value}`, `SF-A2 reply value mismatch for ${value}.`);
      if (reply.providerRid === rid) {
        consecutive += 1;
        if (consecutive >= requiredConsecutive) {
          return;
        }
      } else {
        consecutive = 0;
      }
    } catch {
      // The scenario is waiting for polling-based route convergence after a provider changes.
      consecutive = 0;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`SF-A2 expected request routing to reach ${rid}.`);
}

async function waitForMissingPeer(baseUrl: string, rid: string): Promise<void> {
  const deadline = Date.now() + 10000;
  let last: readonly PeerDto[] = [];
  while (Date.now() < deadline) {
    last = await getJson<PeerDto[]>(baseUrl, '/location/peers');
    if (!last.some((peer) => peer.nodeRid === rid)) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`SF-A2 expected peer ${rid} removal by polling, last=${JSON.stringify(last)}`);
}
