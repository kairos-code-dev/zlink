// SM-F1: route client → target spot 시나리오를 검증한다.
import type {
  CreateSpotRes,
  CreateSpotReq,
  EvidenceWaitReq,
  SpotStateMsgRes,
  SpotStateMsgReq,
  SpotStateRouteReq,
  StateRes
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmF1(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-f1-${Date.now()}`;
  const created = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(created.spotRid === spotRid, 'SM-F1 did not create the requested spot.');
  ensure(created.nodeRid === 'play-a', 'SM-F1 created spot on the wrong node.');

  const state = await postJson<StateRes>(options.playAUrl, '/spot/state/request', {
    spotRid,
    operation: 'add',
    delta: 7
  } satisfies SpotStateRouteReq);
  ensure(state.spotRid === spotRid, 'SM-F1 request reached the wrong spot.');
  ensure(state.nodeRid === 'play-a', 'SM-F1 request reached the wrong node.');
  ensure(state.value === 7, 'SM-F1 state reply mismatch.');

  const command = await postJson<SpotStateMsgRes>(options.playAUrl, '/spot/state/command', {
    spotRid,
    marker: 'sm-f1-command'
  } satisfies SpotStateMsgReq);
  ensure(command.spotRid === spotRid && command.accepted, 'SM-F1 command was not accepted.');

  const expectedEvidence = [
    `spot-state-request|rid=play-a|spot=${spotRid}|value=7`,
    `spot-state-command|rid=play-a|spot=${spotRid}|marker=sm-f1-command`
  ];
  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: expectedEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
    'SM-F1 evidence mismatch.'
  );

  console.log('scenario SM-F1 passed');
}
