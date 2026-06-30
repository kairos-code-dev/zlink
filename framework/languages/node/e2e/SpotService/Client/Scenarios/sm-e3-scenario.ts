import type {
  CreateSpotReply,
  CreateSpotReq,
  EvidenceWaitRequest,
  SpotIdleCloseReply,
  SpotIdleCloseReq,
  SpotMissingTargetReply,
  SpotMissingTargetReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmE3(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-e3-${Date.now()}`;
  const created = await postJson<CreateSpotReply>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(created.spotRid === spotRid && created.nodeRid === 'play-a', 'SM-E3 idle spot was not created on play-a.');

  const idle = await postJson<SpotIdleCloseReply>(options.playAUrl, '/spot/idle-close/start', {
    spotRid,
    name: 'sm-e3-idle',
    periodMs: 50
  } satisfies SpotIdleCloseReq);
  ensure(idle.closed, 'SM-E3 idle close did not close the spot.');

  const closedSpotRequest = await postJson<SpotMissingTargetReply>(options.playAUrl, '/spot/missing-target/request', {
    spotRid
  } satisfies SpotMissingTargetReq);
  ensure(closedSpotRequest.failed, 'SM-E3 closed spot request did not fail.');

  const expectedEvidence = [
    `timer-idle-close|rid=play-a|spot=${spotRid}|name=sm-e3-idle|closed=True`,
    `spot-closing|rid=play-a|spot=${spotRid}`
  ];
  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: expectedEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitRequest);
  ensure(
    expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
    'SM-E3 evidence mismatch.'
  );

  console.log('scenario SM-E3 passed');
}
