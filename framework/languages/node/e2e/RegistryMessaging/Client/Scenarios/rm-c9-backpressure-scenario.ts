import type { ProfileReply } from '../../Shared/messages';
import { postJson } from '../Support/http-client';
import { ensure, uniqueMarker } from '../Support/scenario-assert';

export async function runRmC9(backpressureConsumerUrl: string, providerAUrl: string): Promise<void> {
  await postJson(backpressureConsumerUrl, '/profile/backpressure/reset');
  const marker = uniqueMarker('rm-c9');
  const outcomes = await Promise.all(
    Array.from({ length: 32 }, (_, index) =>
      postJson<string>(
        backpressureConsumerUrl,
        '/profile/backpressure/send',
        { commandId: `rm-c9-slow-${marker}-${index}` }
      ))
  );
  ensure(
    outcomes.some((outcome) => outcome === 'BoundedFailure'),
    'RM-C9 expected at least one bounded failure while the low-HWM socket was saturated.'
  );

  await new Promise((resolve) => setTimeout(resolve, 5000));
  const followUp = await postJson<ProfileReply>(
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
