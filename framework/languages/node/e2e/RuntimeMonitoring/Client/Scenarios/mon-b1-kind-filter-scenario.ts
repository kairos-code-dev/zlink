import type { EvidenceWaitRequest, ProfileReply, ProfileRequest } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonB1(options: ClientOptions): Promise<void> {
  const request: ProfileRequest = { value: 'filter', marker: 'mon-b1-request' };
  const reply = await postJson<ProfileReply>(options.triggerUrl, '/profile/request/service-b', request);
  ensure(reply.providerRid === 'svc-b', 'MON-B1 direct trigger did not hit filtered service.');

  const evidence = await postJson<string[]>(options.serviceBUrl, '/evidence/wait', {
    containsAll: ['monitor-socket|'],
    containsAnyGroups: [['kind=ConnectionReady']],
    timeoutMilliseconds: 15000
  } satisfies EvidenceWaitRequest);

  ensure(
    evidence.some((line) => line.includes('monitor-socket|') && line.includes('kind=ConnectionReady')),
    'MON-B1 filtered socket evidence missing.'
  );
  ensure(
    evidence
      .filter((line) => line.includes('monitor-socket|'))
      .every((line) => line.includes('kind=ConnectionReady')),
    'MON-B1 returned a socket event outside the configured kind filter.'
  );

  console.log('scenario MON-B1 passed');
}
