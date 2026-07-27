// ST-F5: Message Follow route 제거 시나리오를 검증한다.
import type { ProbeReq, ProbeRes } from '../../Shared/messages.js';
import { SpotActorTransferNames, nodeA, nodeB, createSpot, createActor, joinActor, getRef, waitEvidence, post, unique, delay, require } from '../Support/scenario-support';

export async function runStF5(): Promise<void> {
  const actorId = unique('actor-message-follow-chain');
  const spotB = unique('spot-map-chain-b');
  const spotA = unique('spot-map-chain-a');
  await createSpot(nodeB, spotB);
  await createSpot(nodeA, spotA);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 105);
  await getRef(nodeA, actorId);
  require((await joinActor(nodeA, actorId, { scenario: 'ST-F5', targetSpotId: spotB })).accepted, 'ST-F5 first join failed.');
  require((await joinActor(nodeB, actorId, { scenario: 'ST-F5', targetSpotId: spotA })).accepted, 'ST-F5 chained join failed.');
  const response = await post<ProbeRes>(nodeA, `/actors/${actorId}/probe-stale`, {
    scenario: 'ST-F5',
    marker: 'chain'
  } satisfies ProbeReq);
  require(
    response.nodeRid === 'actor-a',
    'ST-F5 chained Message Follow relay did not reach the final target.'
  );
  await waitEvidence(nodeA, [
    `ST-F5|${actorId}|message_follow_registered|500`,
    `ST-F5|${actorId}|message_follow_relay|`
  ]);
  await waitEvidence(nodeB, [
    `ST-F5|${actorId}|message_follow_registered|500`,
    `ST-F5|${actorId}|message_follow_relay|`
  ]);
  await delay(700);
  await waitEvidence(nodeA, [`ST-F5|${actorId}|message_follow_route_removed|`]);
  await waitEvidence(nodeB, [`ST-F5|${actorId}|message_follow_route_removed|`]);
}
