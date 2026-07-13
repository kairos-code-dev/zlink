import type { EvidenceWaitReq, ProfileRes, ProfileReq } from '../../Shared/messages';
import { RuntimeMonitoringNames } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runMonA1(options: ClientOptions): Promise<void> {
  const request: ProfileReq = { value: 'monitor', marker: 'mon-a1-request' };
  const reply = await postJson<ProfileRes>(options.triggerUrl, '/profile/request/disconnect', request);
  ensure(reply.value === 'profile:monitor', 'MON-A1 trigger request failed.');

  const evidence = await postJson<string[]>(options.serviceUrl, '/evidence/wait', {
    containsAll: ['monitor-socket|', `source=${RuntimeMonitoringNames.channelServerSource}`],
    containsAnyGroups: [['kind=connected', 'kind=connectionReady'], ['kind=disconnected', 'kind=closed']],
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);

  ensure(
    evidence.some((line) =>
      line.includes('monitor-socket|')
      && line.includes(`source=${RuntimeMonitoringNames.channelServerSource}`)
      && (line.includes('kind=connected') || line.includes('kind=connectionReady'))),
    'MON-A1 socket connection evidence missing.'
  );
  ensure(
    evidence.some((line) =>
      line.includes('monitor-socket|')
      && (line.includes('kind=disconnected') || line.includes('kind=closed'))),
    'MON-A1 socket disconnect evidence missing.'
  );

  console.log('scenario MON-A1 passed');
}
