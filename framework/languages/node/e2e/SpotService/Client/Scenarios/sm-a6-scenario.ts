// SM-A6: spot lifecycle (initialize·close) 시나리오를 검증한다.
import type { CloseSpotRes, CloseSpotReq, CreateSpotRes, CreateSpotReq, EvidenceWaitReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmA6(options: ClientOptions): Promise<void> {
  const spotRid = `spot-sm-a6-${Date.now().toString(36)}`;
  const created = await postJson<CreateSpotRes>(options.playAUrl, '/spot/create', {
    spotRid
  } satisfies CreateSpotReq);
  ensure(created.spotRid === spotRid, 'SM-A6 lifecycle spot was not created.');
  ensure(created.nodeRid === 'play-a', 'SM-A6 lifecycle spot was created on the wrong node.');

  const closed = await postJson<CloseSpotRes>(options.playAUrl, '/spot/close', {
    spotRid
  } satisfies CloseSpotReq);
  ensure(closed.closed, 'SM-A6 did not close the lifecycle spot.');

  const expected = [
    `spot-initialize|rid=play-a|spot=${spotRid}`,
    `spot-closing|rid=play-a|spot=${spotRid}`
  ];
  const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: expected,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    expected.every((marker) => evidence.some((line) => line.includes(marker))),
    'SM-A6 lifecycle evidence mismatch.'
  );

  console.log('scenario SM-A6 passed');
}
