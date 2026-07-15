// SF-E1: store 응답 지연 중 무관 concurrent 처리 비블로킹 시나리오를 검증한다.
import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJsonWithin, postJsonWithin } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSfE1(options: ClientOptions): Promise<void> {
  const delayedPeerRead = getJsonWithin<Array<{ ownerId: string }>>(options.consumerUrl, '/location/peers', 6000);
  const latencies: number[] = [];
  for (let index = 0; index < 8; index += 1) {
    const startedAt = Date.now();
    const reply = await postJsonWithin<ProfileRes>(
      options.consumerUrl,
      '/profile/request-once',
      { value: `sf-e1-${index}` },
      1200
    );
    latencies.push(Date.now() - startedAt);
    ensure(reply.value === `profile:sf-e1-${index}`, `SF-E1 reply ${index} value mismatch.`);
    ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', `SF-E1 reply ${index} provider mismatch.`);
  }

  const peers = await delayedPeerRead;
  ensure(peers.length >= 2, 'SF-E1 delayed peer read did not recover both providers.');

  const sorted = [...latencies].sort((left, right) => left - right);
  const p99 = sorted[sorted.length - 1] ?? 0;
  ensure(p99 < 1000, `SF-E1 unrelated request latency regressed while store read was delayed: p99=${p99}ms.`);
  console.log('scenario SF-E1 passed');
}
