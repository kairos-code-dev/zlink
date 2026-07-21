// MON-A5: location runtime health 시나리오를 검증한다.
import type { EvidenceWaitReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonA5(options: ClientOptions): Promise<void> {
  await postJson(options.triggerUrl, '/socket/handshake-failure');

  const serviceEvidence = await postJson<string[]>(options.serviceUrl, '/evidence/wait', {
    containsAll: [],
    containsAnyGroups: [
      ['kind=handshakeFailed', 'kind=internal'],
      ['monitor-location|source=monitor.location-runtime|kind=StatusChanged'],
      ['monitor-spot|source=monitor.spot|kind=statusChanged'],
      ['monitor-spot|source=monitor.spot|kind=timerStoppedAfterUnhandledException'],
      ['timer=stopping']
    ],
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);

  ensure(
    serviceEvidence.some((line) =>
      line.includes('monitor-socket|')
      && (line.includes('kind=handshakeFailed') || line.includes('kind=internal'))),
    'MON-A5 handshake failure evidence missing.'
  );
  ensure(
    serviceEvidence.some((line) =>
      line.includes('monitor-location|source=monitor.location-runtime|kind=StatusChanged')),
    'MON-A5 location runtime status evidence missing.'
  );
  ensure(
    serviceEvidence.some((line) =>
      line.includes('monitor-spot|source=monitor.spot|kind=statusChanged')),
    'MON-A5 spot status evidence missing.'
  );
  ensure(
    serviceEvidence.some((line) =>
      line.includes('monitor-spot|source=monitor.spot|kind=timerStoppedAfterUnhandledException')
      && line.includes('timer=stopping')),
    'MON-A5 stopped timer evidence missing.'
  );

  console.log('scenario MON-A5 passed');
}
