// SM-G2: owner 이동 (scale-out 중 spot owner 재배치) 시나리오를 검증한다.
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

export async function runSmG2(options: ClientOptions): Promise<void> {
  const key = `key-sm-g2-${Date.now()}`;
  const firstOwnerSpotRid = `spot-${key}-a`;
  const secondOwnerSpotRid = `spot-${key}-b`;

  const firstCreated = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
    spotRid: firstOwnerSpotRid
  } satisfies CreateSpotReq);
  const firstReply = await postJson<StateRes>(options.playAUrl, '/spot/state/request', {
    spotRid: firstOwnerSpotRid,
    operation: 'add',
    delta: 1
  } satisfies SpotStateRouteReq);
  const firstExpectedEvidence = [`spot-state-request|rid=play-a|spot=${firstOwnerSpotRid}|value=1`];
  const firstEvidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: firstExpectedEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    firstExpectedEvidence.every((expected) => firstEvidence.some((line) => line.includes(expected))),
    'SM-G2 play-a evidence did not include the first owner request.'
  );

  const secondCreated = await postJson<CreateSpotRes>(options.playBUrl, '/spot/create', {
    spotRid: secondOwnerSpotRid
  } satisfies CreateSpotReq);
  const secondReply = await postJson<StateRes>(options.playBUrl, '/spot/state/request', {
    spotRid: secondOwnerSpotRid,
    operation: 'add',
    delta: 1
  } satisfies SpotStateRouteReq);
  const secondExpectedEvidence = [`spot-state-request|rid=play-b|spot=${secondOwnerSpotRid}|value=1`];
  const secondEvidence = await postJson<string[]>(options.playBUrl, '/evidence/wait', {
    containsAll: secondExpectedEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    secondExpectedEvidence.every((expected) => secondEvidence.some((line) => line.includes(expected))),
    'SM-G2 play-b evidence did not include the remapped owner request.'
  );

  ensure(firstCreated.nodeRid === 'play-a', 'SM-G2 first owner was not created on play-a.');
  ensure(secondCreated.nodeRid === 'play-b', 'SM-G2 remapped owner was not created on play-b.');
  ensure(firstReply.nodeRid === 'play-a', 'SM-G2 first owner request reached the wrong node.');
  ensure(secondReply.nodeRid === 'play-b', 'SM-G2 remapped owner request reached the wrong node.');
  ensure(
    !firstEvidence.some((line) => line.includes(`spot-state-request|rid=play-a|spot=${secondOwnerSpotRid}`)),
    'SM-G2 remapped owner leaked to play-a.'
  );
  ensure(
    !secondEvidence.some((line) => line.includes(`spot-state-request|rid=play-b|spot=${firstOwnerSpotRid}`)),
    'SM-G2 first owner leaked to play-b.'
  );

  console.log('scenario SM-G2 passed');
}
