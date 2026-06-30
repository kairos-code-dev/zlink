import type { EvidenceWaitRequest } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonA2(options: ClientOptions): Promise<void> {
  const evidence = await postJson<string[]>(options.registryUrl, '/evidence/wait', {
    containsAll: ['monitor-registry|source=registry'],
    containsAnyGroups: [['kind=TopologyChanged'], ['kind=ServiceSummaryChanged']],
    timeoutMilliseconds: 15000
  } satisfies EvidenceWaitRequest);

  ensure(
    evidence.some((line) =>
      line.includes('monitor-registry|source=registry|kind=TopologyChanged')
      && !line.includes('topology=0')),
    'MON-A2 topology evidence missing.'
  );
  ensure(
    evidence.some((line) =>
      line.includes('monitor-registry|source=registry|kind=ServiceSummaryChanged')
      && !line.includes('summary=0')),
    'MON-A2 service summary evidence missing.'
  );

  console.log('scenario MON-A2 passed');
}
