// ST-A1: local join accept 순서 시나리오를 검증한다.
import { SpotActorTransferNames, nodeA, createSpot, createActor, joinActor, probeActor, waitEvidence, assertOrder, unique, require } from '../Support/scenario-support';

export async function runStA1(): Promise<void> {
  const actorId = unique('actor-local-ok');
  const spotRid = unique('spot-local-ok');
  await createSpot(nodeA, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 11);
  const join = await joinActor(nodeA, actorId, { scenario: 'ST-A1', targetSpotRid: spotRid });
  require(join.accepted, 'ST-A1 join was rejected.');
  const probe = await probeActor(nodeA, actorId, 'ST-A1', 'after-joined');
  require(probe.nodeRid === 'actor-a' && probe.spotRid === spotRid, 'ST-A1 packet did not reach the local user Spot.');
  const entries = await waitEvidence(nodeA, [
    `ST-A1|${actorId}|admission|spot=${spotRid}`,
    `transfer|${actorId}|leave|11`,
    `transfer|${actorId}|joined|${spotRid}:11`,
    `ST-A1|${actorId}|location_committed|node=actor-a|spot=${spotRid}`,
    `ST-A1|${actorId}|success_reply|${spotRid}`,
    `ST-A1|${actorId}|packet_handler|after-joined`
  ]);
  assertOrder(entries, actorId, ['admission', 'leave', 'joined', 'success_reply', 'location_committed', 'packet_handler']);
}
