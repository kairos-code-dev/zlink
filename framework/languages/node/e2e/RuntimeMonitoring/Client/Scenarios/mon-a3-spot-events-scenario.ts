// MON-A3: Channel readiness와 실제 request 결과를 대조한다 시나리오를 검증한다.
import type { EvidenceWaitReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonA3(options: ClientOptions): Promise<void> {
  const evidence = await postJson<string[]>(options.serviceUrl, '/evidence/wait', {
    containsAll: ['monitor-spot|source=monitor.spot'],
    containsAnyGroups: [
      ['kind=timerHandlerFailed'],
      ['timer=failing']
    ],
    timeoutMilliseconds: 15000
  } satisfies EvidenceWaitReq);

  ensure(
    evidence.some((line) =>
      line.includes('monitor-spot|source=monitor.spot|kind=timerHandlerFailed')
      && line.includes('timer=failing')),
    'MON-A3 spot timer failure evidence missing.'
  );

  console.log('scenario MON-A3 passed');
}
