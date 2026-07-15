// ST-C1: source down after admission before commit 시나리오를 검증한다.
import { SpotActorTransferNames, nodeA, nodeB, createSpot, createActor, joinActor, getEvidence, waitEvidence, post, has, unique, delay, require } from '../Support/scenario-support';

export async function runStC1(): Promise<void> {
  const actorId = unique('actor-source-down-before-commit');
  const spotRid = unique('spot-source-down-before-commit');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 62);
  const join = joinActor(nodeA, actorId, { scenario: 'ST-C1', targetSpotRid: spotRid }).catch(() => undefined);
  await waitEvidence(nodeB, [`ST-C1|${actorId}|admission|spot=${spotRid}`]);
  await waitEvidence(nodeA, [`ST-C1|${actorId}|before_commit_gate|62`]);
  await post(nodeA, '/shutdown', {});
  await join;
  await delay(31_000);
  const entries = await getEvidence(nodeB);
  require(!has(entries, actorId, 'joined'), 'ST-C1 target joined after source died before commit.');
  require(!has(entries, actorId, 'transfer_in'), 'ST-C1 transferIn ran without commit.');
}
