// SM-A5: Stage wrapper 시나리오를 검증한다.
import type {
  CloseSpotRes,
  CloseSpotReq,
  CreateSpotRes,
  CreateSpotReq,
  EvidenceWaitReq,
  SpotStageProbeReq,
  SpotStageTimerRes,
  SpotStageTimerReq,
  SpotStateRouteReq,
  StateRes
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmA5(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-a5-${Date.now()}`;
  const created = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(created.spotRid === spotRid, 'SM-A5 did not create the requested spot.');
  ensure(created.nodeRid === 'play-a', 'SM-A5 created spot on the wrong node.');

  const ready = await postJson<StateRes>(options.playAUrl, '/spot/state/request', {
    spotRid,
    operation: 'noop',
    delta: 0
  } satisfies SpotStateRouteReq);
  ensure(
    ready.spotRid === spotRid && ready.nodeRid === 'play-a',
    'SM-A5 spot route did not become ready.'
  );

  const probeReply = await postJson<StateRes>(options.playAUrl, '/spot/stage/request', {
    spotRid,
    marker: 'sm-a5-stage',
    delta: 9
  } satisfies SpotStageProbeReq);
  ensure(probeReply.spotRid === spotRid, 'SM-A5 stage request reached the wrong spot.');
  ensure(probeReply.nodeRid === 'play-a', 'SM-A5 stage request reached the wrong node.');
  ensure(probeReply.value === 9, 'SM-A5 stage request state mismatch.');

  const timer = await postJson<SpotStageTimerRes>(options.playAUrl, '/spot/stage/timer', {
    spotRid,
    name: 'sm-a5-stage-timer',
    periodMs: 50
  } satisfies SpotStageTimerReq);
  ensure(timer.spotRid === spotRid && timer.started, 'SM-A5 stage timer was not started.');

  const closeReply = await postJson<CloseSpotRes>(options.playAUrl, '/spot/close', {
    spotRid
  } satisfies CloseSpotReq);
  ensure(closeReply.closed, 'SM-A5 did not close the spot.');

  const expectedEvidence = [
    `spot-initialize|rid=play-a|spot=${spotRid}`,
    `stage-request|rid=play-a|spot=${spotRid}|marker=sm-a5-stage|value=9`,
    `stage-timer|rid=play-a|spot=${spotRid}|name=sm-a5-stage-timer`,
    `spot-closing|rid=play-a|spot=${spotRid}`
  ];
  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: expectedEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
    'SM-A5 evidence mismatch.'
  );

  console.log('scenario SM-A5 passed');
}
