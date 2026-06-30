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

export async function runSmA2(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-a2-${Date.now()}`;
  const created = await postJson<CreateSpotReply>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(created.spotRid === spotRid, 'SM-A2 did not create the requested spot.');

  const reply = await postJson<StateReply>(options.playAUrl, '/spot/state/request', {
    spotRid,
    operation: 'add',
    delta: 1
  } satisfies SpotStateRouteReq);
  ensure(reply.spotRid === spotRid, 'SM-A2 state request reached the wrong spot.');
  ensure(reply.nodeRid === 'play-a', 'SM-A2 state request reached the wrong node.');
  ensure(reply.value === 1, 'SM-A2 state mutation reply mismatch.');

  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: [`spot-state-request|rid=play-a|spot=${spotRid}|value=1`],
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitRequest);
  ensure(
    evidence.some((line) => line.includes(`spot-state-request|rid=play-a|spot=${spotRid}|value=1`)),
    'SM-A2 state mutation evidence missing.'
  );

  console.log('scenario SM-A2 passed');
}
