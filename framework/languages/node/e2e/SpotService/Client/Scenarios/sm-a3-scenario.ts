import type {
  CreateSpotReply,
  CreateSpotReq,
  EvidenceWaitRequest,
  SpotStateRouteReq,
  StateReply
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmA3(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-a3-${Date.now()}`;
  const created = await postJson<CreateSpotReply>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(
    created.spotRid === spotRid && created.nodeRid === 'play-a',
    'SM-A3 routed spot was not created on play-a.'
  );

  const routeReply = await postJson<StateReply>(options.playAUrl, '/spot/state/request', {
    spotRid,
    operation: 'add',
    delta: 1
  } satisfies SpotStateRouteReq);
  ensure(routeReply.spotRid === spotRid, 'SM-A3 route reached the wrong spot.');
  ensure(routeReply.nodeRid === 'play-a', 'SM-A3 route reached the wrong node.');
  ensure(routeReply.value === 1, 'SM-A3 state reply mismatch.');

  const expectedPlayAEvidence = [`spot-state-request|rid=play-a|spot=${spotRid}|value=1`];
  const playAEvidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: expectedPlayAEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitRequest);
  ensure(
    expectedPlayAEvidence.every((expected) => playAEvidence.some((line) => line.includes(expected))),
    'SM-A3 evidence mismatch.'
  );

  const playBEvidence = await postJson<string[]>(options.playBUrl, '/evidence/wait', {
    containsAll: [],
    timeoutMilliseconds: 100
  } satisfies EvidenceWaitRequest);
  ensure(
    playBEvidence.every((line) => !line.includes(`spot-state-request|rid=play-b|spot=${spotRid}`)),
    'SM-A3 route resolver leaked the spot request to play-b.'
  );

  console.log('scenario SM-A3 passed');
}
