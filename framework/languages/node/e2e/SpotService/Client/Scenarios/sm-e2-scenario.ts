// SM-E2: spot timer 시나리오를 검증한다.
import type {
  CloseSpotRes,
  CloseSpotReq,
  CreateSpotRes,
  CreateSpotReq,
  EvidenceWaitReq,
  SpotTimerStartRes,
  SpotTimerStartReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmE2(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-e2-${Date.now().toString(36)}`;
  const created = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(created.spotRid === spotRid, 'SM-E2 timer spot was not created.');
  ensure(created.nodeRid === 'play-a', 'SM-E2 timer spot was created on the wrong node.');

  const timer = await postJson<SpotTimerStartRes>(options.playAUrl, '/spot/timer/start', {
    spotRid,
    name: 'sm-e2-basic',
    periodMs: 50
  } satisfies SpotTimerStartReq);
  ensure(timer.started, 'SM-E2 timer did not start.');

  const closeReply = await postJson<CloseSpotRes>(options.playAUrl, '/spot/close', {
    spotRid
  } satisfies CloseSpotReq);
  ensure(closeReply.closed, 'SM-E2 timer spot close reply mismatch.');

  const marker = `timer-basic|rid=play-a|spot=${spotRid}|name=sm-e2-basic`;
  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: [marker],
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    evidence.filter((line) => line.includes(marker)).length >= 2,
    'SM-E2 evidence mismatch.'
  );

  console.log('scenario SM-E2 passed');
}
