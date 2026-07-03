import type { EvidenceWaitReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonA5(options: ClientOptions): Promise<void> {
  await postJson(options.triggerUrl, '/socket/handshake-failure');

  const serviceEvidence = await postJson<string[]>(options.serviceUrl, '/evidence/wait', {
    containsAll: [],
    containsAnyGroups: [
      ['kind=HandshakeFailed', 'kind=Internal'],
      ['monitor-location|source=monitor.location-runtime|kind=StatusChanged'],
      ['monitor-spot|source=monitor.spot|kind=StatusChanged'],
      ['monitor-spot|source=monitor.spot|kind=TimerStoppedAfterUnhandledException'],
      ['timer=stopping']
    ],
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);

  ensure(
    serviceEvidence.some((line) =>
      line.includes('monitor-socket|')
      && (line.includes('kind=HandshakeFailed') || line.includes('kind=Internal'))),
    'MON-A5 handshake failure evidence missing.'
  );
  ensure(
    serviceEvidence.some((line) =>
      line.includes('monitor-location|source=monitor.location-runtime|kind=StatusChanged')),
    'MON-A5 location runtime status evidence missing.'
  );
  ensure(
    serviceEvidence.some((line) =>
      line.includes('monitor-spot|source=monitor.spot|kind=StatusChanged')),
    'MON-A5 spot status evidence missing.'
  );
  ensure(
    serviceEvidence.some((line) =>
      line.includes('monitor-spot|source=monitor.spot|kind=TimerStoppedAfterUnhandledException')
      && line.includes('timer=stopping')),
    'MON-A5 stopped timer evidence missing.'
  );

  console.log('scenario MON-A5 passed');
}
