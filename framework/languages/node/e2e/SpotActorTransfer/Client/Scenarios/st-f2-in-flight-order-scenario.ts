// ST-F2: direct overtakes prevented 시나리오를 검증한다.
import { SpotActorTransferNames, nodeA, nodeB, createSpot, createActor, joinActor, sendHandoff, waitEvidence, post, assertOrder, unique, require } from '../Support/scenario-support';

export async function runStF2(): Promise<void> {
  const actorId = unique('actor-handoff-gate-f2');
  const spotRid = unique('spot-handoff-overtake');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 102);
  const join = joinActor(nodeA, actorId, { scenario: 'ST-F2', targetSpotRid: spotRid });
  await waitEvidence(nodeA, [`ST-F2|${actorId}|before_commit_gate|102`]);
  await sendHandoff(nodeA, actorId, 'ST-F2', 'B1');
  await sendHandoff(nodeA, actorId, 'ST-F2', 'B2');
  await post(nodeA, `/transfer-gates/${actorId}/release`, {});
  require((await join).accepted, 'ST-F2 join failed.');
  await sendHandoff(nodeB, actorId, 'ST-F2', 'D1');
  const entries = await waitEvidence(nodeB, [
    `ST-F2|${actorId}|backlog_enqueued|0`,
    `ST-F2|${actorId}|packet_handler|B1`,
    `ST-F2|${actorId}|backlog_enqueued|1`,
    `ST-F2|${actorId}|packet_handler|B2`,
    `ST-F2|${actorId}|location_committed|node=actor-b|spot=${spotRid}`,
    `ST-F2|${actorId}|packet_handler|D1`
  ]);
  assertOrder(entries, actorId, [
    'backlog_enqueued',
    'packet_handler',
    'backlog_enqueued',
    'packet_handler',
    'location_committed',
    'packet_handler'
  ]);
}
