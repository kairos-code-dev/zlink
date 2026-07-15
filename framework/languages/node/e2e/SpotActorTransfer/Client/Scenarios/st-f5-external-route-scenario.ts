// ST-F5: forwarding mapping eviction 시나리오를 검증한다.
import type { ProbeReq, ProbeRes } from '../../Shared/messages.js';
import { SpotActorTransferNames, nodeA, nodeB, createSpot, createActor, joinActor, getRef, waitEvidence, post, unique, delay, require } from '../Support/scenario-support';

export async function runStF5(): Promise<void> {
  const actorId = unique('actor-map-chain');
  const spotB = unique('spot-map-chain-b');
  const spotA = unique('spot-map-chain-a');
  await createSpot(nodeB, spotB);
  await createSpot(nodeA, spotA);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 105);
  await getRef(nodeA, actorId);
  require((await joinActor(nodeA, actorId, { scenario: 'ST-F5', targetSpotRid: spotB })).accepted, 'ST-F5 first join failed.');
  require((await joinActor(nodeB, actorId, { scenario: 'ST-F5', targetSpotRid: spotA })).accepted, 'ST-F5 chained join failed.');
  const response = await post<ProbeRes>(nodeA, `/actors/${actorId}/probe-stale`, {
    scenario: 'ST-F5',
    marker: 'chain'
  } satisfies ProbeReq);
  require(response.nodeRid === 'actor-a', 'ST-F5 chained straggler did not reach final target.');
  await waitEvidence(nodeA, [
    `ST-F5|${actorId}|forwarding_mapped|500`,
    `ST-F5|${actorId}|straggler_forward|`
  ]);
  await waitEvidence(nodeB, [
    `ST-F5|${actorId}|forwarding_mapped|500`,
    `ST-F5|${actorId}|straggler_forward|`
  ]);
  await delay(700);
  await waitEvidence(nodeA, [`ST-F5|${actorId}|mapping_evicted|`]);
  await waitEvidence(nodeB, [`ST-F5|${actorId}|mapping_evicted|`]);
}
