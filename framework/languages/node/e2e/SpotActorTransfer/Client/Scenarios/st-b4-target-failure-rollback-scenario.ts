// ST-B4: remote relocation empty state 시나리오를 검증한다.
import { SpotActorTransferNames, nodeA, nodeB, createSpot, createActor, joinActor, probeActor, waitEvidence, assertOrder, mergeEvidence, unique, require } from '../Support/scenario-support';

export async function runStB4(): Promise<void> {
  const actorId = unique('actor-empty-state');
  const spotRid = unique('spot-empty-state');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeEmptyState, 41);
  require((await joinActor(nodeA, actorId, { scenario: 'ST-B4', targetSpotRid: spotRid })).accepted, 'ST-B4 join failed.');
  const probe = await probeActor(nodeB, actorId, 'ST-B4', 'after-empty-state-transfer');
  require(probe.stateVersion === 41, 'ST-B4 domain state was not loaded after joined.');
  const source = await waitEvidence(nodeA, [
    `transfer|${actorId}|transfer_out_empty|custom-adapter`,
    `ST-B4|${actorId}|commit_request|after-source-leave`,
    `ST-B4|${actorId}|commit_ack|${spotRid}`,
    `ST-B4|${actorId}|success_reply|${spotRid}`
  ]);
  const target = await waitEvidence(nodeB, [
    `ST-B4|${actorId}|admission|spot=${spotRid}`,
    `transfer|${actorId}|transfer_in_empty|custom-adapter`,
    `transfer|${actorId}|joined|${spotRid}:0`,
    `transfer|${actorId}|domain_state_loaded|${actorId}`,
    `ST-B4|${actorId}|location_committed|node=actor-b|spot=${spotRid}`
  ]);
  assertOrder(source, actorId, [
    'transfer_out_empty', 'leave', 'commit_request', 'commit_ack', 'success_reply'
  ]);
  assertOrder(target, actorId, [
    'admission', 'transfer_in_empty', 'joined', 'domain_state_loaded', 'location_committed'
  ]);
  assertOrder(mergeEvidence(source, target), actorId, [
    'location_committed', 'commit_ack', 'success_reply'
  ]);
}
