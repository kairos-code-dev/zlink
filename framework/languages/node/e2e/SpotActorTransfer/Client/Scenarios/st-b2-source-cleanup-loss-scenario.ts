// ST-B2: source cleanup 실패는 성공을 되돌리지 않음 시나리오를 검증한다.
import { SpotActorTransferNames, nodeA, nodeB, createSpot, createActor, joinActor, probeActor, post, unique, delay, require } from '../Support/scenario-support';

export async function runStB2(): Promise<void> {
  const actorId = unique('actor-cleanup-after-success');
  const spotRid = unique('spot-cleanup-after-success');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 22);
  require((await joinActor(nodeA, actorId, { scenario: 'ST-B2', targetSpotRid: spotRid })).accepted, 'ST-B2 join failed.');
  await post(nodeA, '/shutdown', {});
  await delay(1500);
  const probe = await probeActor(nodeB, actorId, 'ST-B2', 'after-source-cleanup-loss');
  require(probe.nodeRid === 'actor-b' && probe.stateVersion === 22, 'ST-B2 target ownership was lost.');
}
