import type { EvidenceWaitReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonA3(options: ClientOptions): Promise<void> {
  const evidence = await postJson<string[]>(options.serviceUrl, '/evidence/wait', {
    containsAll: ['monitor-spot|source=monitor.spot'],
    containsAnyGroups: [
      ['kind=StatusChanged'],
      ['kind=PeersChanged'],
      ['kind=SubjectsChanged'],
      ['kind=TimerHandlerFailed'],
      ['timer=failing']
    ],
    timeoutMilliseconds: 15000
  } satisfies EvidenceWaitReq);

  ensure(
    evidence.some((line) => line.includes('monitor-spot|source=monitor.spot|kind=StatusChanged')),
    'MON-A3 spot status evidence missing.'
  );
  ensure(
    evidence.some((line) => line.includes('monitor-spot|source=monitor.spot|kind=PeersChanged')),
    'MON-A3 spot peer evidence missing.'
  );
  ensure(
    evidence.some((line) => line.includes('monitor-spot|source=monitor.spot|kind=SubjectsChanged')),
    'MON-A3 spot subject evidence missing.'
  );
  ensure(
    evidence.some((line) =>
      line.includes('monitor-spot|source=monitor.spot|kind=TimerHandlerFailed')
      && line.includes('timer=failing')),
    'MON-A3 spot timer failure evidence missing.'
  );

  console.log('scenario MON-A3 passed');
}
