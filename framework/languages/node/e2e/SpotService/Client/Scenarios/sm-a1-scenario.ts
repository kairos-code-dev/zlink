// SM-A1: entry spot 생성과 request 시나리오를 검증한다.
import type { CreateSpotRes, CreateSpotReq, EvidenceWaitReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmA1(options: ClientOptions): Promise<void> {
  const spotRid = 'sm-a1-user';
  const created = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(created.spotRid === spotRid, 'SM-A1 did not create the requested spot.');
  ensure(created.nodeRid === 'play-a', 'SM-A1 created spot on the wrong node.');

  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: [`create-spot|rid=play-a|spot=${spotRid}`],
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    evidence.some((line) => line.includes(`create-spot|rid=play-a|spot=${spotRid}`)),
    'SM-A1 create-spot evidence missing.'
  );

  console.log('scenario SM-A1 passed');
}
