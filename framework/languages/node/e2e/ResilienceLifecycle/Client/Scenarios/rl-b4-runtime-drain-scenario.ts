// RL-B4: 런타임 drain / restore (무중단 배포) 시나리오를 검증한다.
import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import { profileReq } from '../Support/resilience-helpers';
import { countNewEvidence, ensure } from '../Support/scenario-assert';

export async function runRlB4(options: ClientOptions): Promise<void> {
  const beforeDrain = await getJson<string[]>(options.providerBUrl, '/evidence');
  await postJson(options.providerBUrl, '/admin/drain');
  await waitForWeight(options.providerBUrl, 0);

  for (let i = 0; i < 20; i += 1) {
    const marker = `rl-b4-drained-${i}`;
    const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', profileReq(marker));
    ensure(reply.providerRid === 'api-a', 'RL-B4 drained api-b received a new request.');
  }

  const afterDrain = await getJson<string[]>(options.providerBUrl, '/evidence');
  ensure(
    countNewEvidence(afterDrain, beforeDrain, 'profile-request|rid=api-b', 'marker=rl-b4-drained-') === 0,
    'RL-B4 api-b evidence changed after drain.'
  );
  await postJson<string[]>(options.providerAUrl, '/evidence/wait', {
    contains: 'profile-request|rid=api-a|value=fast|marker=rl-b4-drained-'
  });

  await postJson(options.providerBUrl, '/admin/restore');
  await waitForWeight(options.providerBUrl, 100);

  for (let i = 0; i < 40; i += 1) {
    const reply = await postJson<ProfileRes>(
      options.consumerUrl,
      '/profile/request',
      profileReq(`rl-b4-restored-${i}`)
    );
    ensure(reply.value === 'profile:fast', 'RL-B4 restored request returned an unexpected value.');
  }

  await postJson<string[]>(options.providerBUrl, '/evidence/wait', {
    contains: 'profile-request|rid=api-b|value=fast|marker=rl-b4-restored-'
  });
  console.log('scenario RL-B4 passed');
}

async function waitForWeight(providerUrl: string, expected: number): Promise<void> {
  await postJson(providerUrl, '/admin/weight/wait', { expected });
}
