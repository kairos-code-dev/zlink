// SM-F6: 같은 MeshName의 다중 MeshNode에서 원격 Spot·Actor 도달 시나리오를 검증한다.
import type {
  EvidenceWaitReq,
  CreateSpotReq,
  CreateSpotRes,
  SpotOnlyJoinReq,
  SpotOnlyJoinRes,
  SpotOnlyMeshReq,
  SpotOnlyMeshRes
} from '../../Shared/messages';
import { SpotServiceNames } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmF6(options: ClientOptions): Promise<void> {
  const sourceSpotRid = `spot-sm-f6-source-${Date.now()}`;
  const targetSpotRid = `spot-sm-f6-target-${Date.now()}`;
  const actorId = `actor-sm-f6-${Date.now()}`;
  const marker = `sm-f6-${Date.now()}`;

  const created = await postJson<CreateSpotRes>(options.multiBUrl, '/spot/create-user-local', {
    spotRid: targetSpotRid
  } satisfies CreateSpotReq);
  ensure(created.nodeRid === SpotServiceNames.multiSpotNodeB, 'SM-F6 target spot node mismatch.');

  const mesh = await postJson<SpotOnlyMeshRes>(options.multiAUrl, '/spot/spot-only/request-send', {
    sourceSpotRid,
    targetSpotRid,
    marker
  } satisfies SpotOnlyMeshReq);
  ensure(mesh.targetSpotRid === targetSpotRid, 'SM-F6 request target mismatch.');
  ensure(mesh.targetValue === 7, 'SM-F6 target request value mismatch.');

  const join = await postJson<SpotOnlyJoinRes>(options.multiAUrl, '/actor/spot-only-join', {
    targetSpotRid,
    actorId,
    marker
  } satisfies SpotOnlyJoinReq);
  ensure(join.accepted, 'SM-F6 spot-only actor join was rejected.');
  ensure(join.actorId === actorId, 'SM-F6 actor join id mismatch.');

  const expected = [
    `spot-state-request|rid=${SpotServiceNames.multiSpotNodeB}|spot=${targetSpotRid}|value=7`,
    `spot-state-command|rid=${SpotServiceNames.multiSpotNodeB}|spot=${targetSpotRid}|marker=sm-f6-send-${marker}`,
    `spot-actor-joined|rid=${SpotServiceNames.multiSpotNodeB}|spot=${targetSpotRid}|actor=${actorId}`
  ];
  const evidence = await postJson<string[]>(options.multiBUrl, '/evidence/wait', {
    containsAll: expected,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    evidence.some((line) => line.includes(`spot-actor-joined|rid=${SpotServiceNames.multiSpotNodeB}|spot=${targetSpotRid}|actor=${actorId}`)),
    'SM-F6 remote actor join evidence did not appear on node B.'
  );

  console.log('scenario SM-F6 passed');
}
