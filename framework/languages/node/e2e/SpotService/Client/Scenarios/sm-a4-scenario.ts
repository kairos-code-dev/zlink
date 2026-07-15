// SM-A4: owner routing — key → RoutingId 매핑 시나리오를 검증한다.
import type {
  CreateSpotRes,
  CreateSpotReq,
  EvidenceWaitReq,
  SpotStateRouteReq,
  StateRes
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmA4(options: ClientOptions): Promise<void> {
  const spotRid = 'sm-a1-user';
  const created = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(
    created.spotRid === spotRid && created.nodeRid === 'play-a',
    'SM-A4 owner spot was not created on play-a.'
  );

  const reply = await postJson<StateRes>(options.playAUrl, '/spot/state/request', {
    spotRid,
    operation: 'noop',
    delta: 0
  } satisfies SpotStateRouteReq);
  ensure(reply.spotRid === spotRid, 'SM-A4 request reached the wrong spot.');
  ensure(reply.nodeRid === 'play-a', 'SM-A4 owner routing did not stay on play-a.');

  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: [`spot-state-request|rid=play-a|spot=${spotRid}|value=${reply.value}`],
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    evidence.some((line) => line.includes(`spot-state-request|rid=play-a|spot=${spotRid}|value=${reply.value}`)),
    'SM-A4 owner routing evidence missing.'
  );

  console.log('scenario SM-A4 passed');
}
