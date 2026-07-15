// RM-C9: backpressure / HWM 포화 시나리오를 검증한다.
import type { ProfileRes } from '../../Shared/messages';
import { postJson } from '../Support/http-client';
import { ensure, uniqueMarker } from '../Support/scenario-assert';

const slowSendCount = 8;

export async function runRmC9(backpressureConsumerUrl: string, providerAUrl: string): Promise<void> {
  await postJson(backpressureConsumerUrl, '/profile/backpressure/reset');
  const marker = uniqueMarker('rm-c9');
  const outcomes = await Promise.all(
    Array.from({ length: slowSendCount }, (_, index) =>
      postJson<string>(
        backpressureConsumerUrl,
        '/profile/backpressure/send',
        { commandId: `rm-c9-slow-${marker}-${index}` }
      ))
  );
  ensure(
    outcomes.every((outcome) => outcome === 'Submitted'),
    'RM-C9 expected all one-way sends to be submitted without a public bounded-failure oracle.'
  );

  await new Promise((resolve) => setTimeout(resolve, 10000));
  const followUp = await postJson<ProfileRes>(
    backpressureConsumerUrl,
    '/profile/request',
    { value: 'rm-c9-after' }
  );
  ensure(followUp.value === 'profile:rm-c9-after', 'RM-C9 follow-up request failed after backlog cleared.');

  const evidence = await postJson<string[]>(
    providerAUrl,
    '/evidence/wait',
    { contains: 'rm-c9-after', timeoutMilliseconds: 20000 }
  );
  ensure(
    evidence.some((line) => line.includes('rm-c9-after')),
    'RM-C9 recovery evidence missing.'
  );
  console.log('scenario RM-C9 passed');
}
