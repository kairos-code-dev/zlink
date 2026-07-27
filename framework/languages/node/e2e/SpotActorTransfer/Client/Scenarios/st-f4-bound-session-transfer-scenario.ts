// ST-F4: Message Follow duration 안의 relay와 만료 뒤 fail-fast를 검증한다.
import type { ProbeReq } from '../../Shared/messages.js';
import { SpotActorTransferNames, nodeA, nodeB, createSpot, createActor, joinActor, getRef, getEvidence, waitEvidence, post, unique, delay, require } from '../Support/scenario-support';

export async function runStF4(): Promise<void> {
  const actorId = unique('actor-message-follow');
  const spotId = unique('spot-message-follow');
  await createSpot(nodeB, spotId);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 104);
  await getRef(nodeA, actorId);
  require((await joinActor(nodeA, actorId, { scenario: 'ST-F4', targetSpotId: spotId })).accepted, 'ST-F4 join failed.');
  await waitEvidence(nodeA, [`ST-F4|${actorId}|message_follow_registered|500`]);
  await post(nodeA, `/actors/${actorId}/probe-stale`, { scenario: 'ST-F4', marker: 'G1' } satisfies ProbeReq);
  await waitEvidence(nodeB, [`ST-F4|${actorId}|packet_handler|G1`]);
  await delay(700);
  let failed = false;
  try {
    await post(nodeA, `/actors/${actorId}/probe-stale`, { scenario: 'ST-F4', marker: 'G2' } satisfies ProbeReq);
  } catch {
    failed = true;
  }
  require(failed, 'ST-F4 old ref packet after cutoff did not fail fast.');
  const source = await waitEvidence(nodeA, [
    `ST-F4|${actorId}|message_follow_relay|`,
    `ST-F4|${actorId}|message_follow_route_removed|`,
    `ST-F4|${actorId}|message_follow_rejected|`
  ]);
  require(source.length > 0, 'ST-F4 Message Follow evidence missing.');
  require(
    !(await getEvidence(nodeB)).some((entry) => entry.actorId === actorId && entry.value === 'G2'),
    'ST-F4 cutoff packet reached target.'
  );
}
