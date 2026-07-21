// MON-C1: claim progress와 observer 격리 시나리오를 검증한다.
import type { EvidenceWaitReq, ProfileRes, ProfileReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonC1(options: ClientOptions): Promise<void> {
  const request: ProfileReq = { value: 'throw', marker: 'mon-c1-request' };
  const failureReply = await postJson<ProfileRes>(options.triggerUrl, '/profile/request/throw', request);
  ensure(failureReply.providerRid === 'svc-throw', 'MON-C1 direct trigger did not hit throwing-monitor service.');

  const [throwEvidence, throwStderr] = await waitForDispatchFailureEvidence(options);

  const recoveryReply = await postJson<ProfileRes>(options.triggerUrl, '/profile/request/throw', {
    value: 'throw',
    marker: 'mon-c1-recovery'
  } satisfies ProfileReq);
  ensure(recoveryReply.value === 'profile:throw', 'MON-C1 messaging did not recover after monitoring handler failure.');

  ensure(
    throwEvidence.some((line) => line.includes('monitor-socket|')),
    'MON-C1 socket evidence missing.'
  );
  ensure(
    throwEvidence.some((line) => line.includes('monitor-throw|')),
    'MON-C1 throwing monitor evidence missing.'
  );
  ensure(
    throwStderr.some((line) => line.includes('monitoring-event-dispatch'))
    && throwStderr.some((line) => line.includes('monitoring dispatch failure for e2e')),
    'MON-C1 dispatch failure log evidence missing.'
  );

  console.log('scenario MON-C1 passed');
}

async function waitForDispatchFailureEvidence(options: ClientOptions): Promise<[string[], string[]]> {
  return await Promise.all([
    postJson<string[]>(options.throwServiceUrl, '/evidence/wait', {
      containsAll: [],
      containsAnyGroups: [['monitor-socket|'], ['monitor-throw|']],
      timeoutMilliseconds: 15000
    } satisfies EvidenceWaitReq),
    postJson<string[]>(options.triggerUrl, '/logs/throw-stderr/wait', {
      containsAll: [],
      containsAnyGroups: [['monitoring-event-dispatch'], ['monitoring dispatch failure for e2e']],
      timeoutMilliseconds: 15000
    } satisfies EvidenceWaitReq)
  ]);
}
