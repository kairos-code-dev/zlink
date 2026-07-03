import type { EvidenceWaitReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonA2(options: ClientOptions): Promise<void> {
  const evidence = await postJson<string[]>(options.serviceUrl, '/evidence/wait', {
    containsAll: ['monitor-location|source=monitor.location-runtime'],
    containsAnyGroups: [['kind=TopologyChanged'], ['kind=ServiceSummaryChanged']],
    timeoutMilliseconds: 15000
  } satisfies EvidenceWaitReq);

  ensure(
    evidence.some((line) =>
      line.includes('monitor-location|source=monitor.location-runtime|kind=TopologyChanged')
      && !line.includes('topology=0')),
    'MON-A2 topology evidence missing.'
  );
  ensure(
    evidence.some((line) =>
      line.includes('monitor-location|source=monitor.location-runtime|kind=ServiceSummaryChanged')
      && !line.includes('summary=0')),
    'MON-A2 service summary evidence missing.'
  );

  console.log('scenario MON-A2 passed');
}
