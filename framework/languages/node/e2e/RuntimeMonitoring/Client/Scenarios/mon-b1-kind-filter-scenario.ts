// MON-B1: event kind 필터 시나리오를 검증한다.
import type { EvidenceWaitReq, ProfileRes, ProfileReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonB1(options: ClientOptions): Promise<void> {
  const request: ProfileReq = { value: 'filter', marker: 'mon-b1-request' };
  const reply = await postJson<ProfileRes>(options.triggerUrl, '/profile/request/service-b', request);
  ensure(reply.providerRid === 'svc-b', 'MON-B1 direct trigger did not hit filtered service.');

  const evidence = await postJson<string[]>(options.serviceBUrl, '/evidence/wait', {
    containsAll: ['monitor-socket|'],
    containsAnyGroups: [['kind=connectionReady']],
    timeoutMilliseconds: 15000
  } satisfies EvidenceWaitReq);

  ensure(
    evidence.some((line) => line.includes('monitor-socket|') && line.includes('kind=connectionReady')),
    'MON-B1 filtered socket evidence missing.'
  );
  ensure(
    evidence
      .filter((line) => line.includes('monitor-socket|'))
      .every((line) => line.includes('kind=connectionReady')),
    'MON-B1 returned a socket event outside the configured kind filter.'
  );

  console.log('scenario MON-B1 passed');
}
