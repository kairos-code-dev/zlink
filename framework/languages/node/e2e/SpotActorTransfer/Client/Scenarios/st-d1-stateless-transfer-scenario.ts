// ST-D1: location commit 시점 시나리오를 검증한다.
import { SpotActorTransferNames, nodeA, nodeB, createSpot, createActor, joinActor, getRef, waitEvidence, post, unique, delay, require } from '../Support/scenario-support';

export async function runStD1(): Promise<void> {
  const actorId = unique('actor-location-delay');
  const spotRid = unique('spot-location-delay');
  await createSpot(nodeB, spotRid, 'delay-joined');
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 81);
  const join = joinActor(nodeA, actorId, { scenario: 'ST-D1', targetSpotRid: spotRid });
  await waitEvidence(nodeB, [`ST-D1|${actorId}|joined_wait|${spotRid}`]);
  const pending = await getRef(nodeA, actorId);
  require(pending.nodeRid === 'actor-a', 'ST-D1 target location became public before joined completed.');
  await post(nodeB, `/joined-gates/${spotRid}/release`, {});
  require((await join).accepted, 'ST-D1 join failed.');
  const committed = await getRef(nodeB, actorId);
  require(committed.nodeRid === 'actor-b', 'ST-D1 committed target location is missing.');
}
