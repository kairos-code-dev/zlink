// RL-D2: observer 실패 격리 시나리오를 검증한다.
import type { ProfileRes, RequestFailureRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../../../http-client';
import { profileReq } from '../Support/resilience-helpers';
import { ensure } from '../Support/scenario-assert';

export async function runRlD2(options: ClientOptions): Promise<void> {
  await postJson(options.providerAUrl, '/admin/fault/observer-throws');
  await postJson(options.providerBUrl, '/admin/fault/observer-throws');

  const missing = await postJson<RequestFailureRes>(
    options.consumerUrl,
    '/profile/missing-request',
    profileReq('rl-d2-error')
  );
  ensure(missing.failed, 'RL-D2 missing handler request should fail.');

  await waitForEitherEvidence(options, 'dispatch-error', 'RL-D2 did not record dispatch-error evidence.');

  const followUp = await postJson<ProfileRes>(
    options.consumerUrl,
    '/profile/request',
    profileReq('rl-d2-after')
  );
  ensure(followUp.value === 'profile:fast', 'RL-D2 messaging did not continue after observer failure.');

  await postJson(options.providerAUrl, '/admin/fault/none');
  await postJson(options.providerBUrl, '/admin/fault/none');

  await waitForEitherEvidence(options, 'marker=rl-d2-after', 'RL-D2 did not record expected follow-up evidence.');

  console.log('scenario RL-D2 passed');
}

async function waitForEitherEvidence(options: ClientOptions, contains: string, message: string): Promise<void> {
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    const snapshots = await Promise.allSettled([
      getJson<string[]>(options.providerAUrl, '/evidence'),
      getJson<string[]>(options.providerBUrl, '/evidence')
    ]);
    if (snapshots.some((snapshot) =>
      snapshot.status === 'fulfilled' && snapshot.value.some((line) => line.includes(contains)))) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  ensure(false, message);
}
