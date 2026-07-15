// ST-E1: remote transfer 뒤 bound session push 시나리오를 검증한다.
import { SpotActorTransferNames, options, nodeA, nodeB, connectAndBind, assertBoundPush, assertHttpBoundPush, createSpot, createActor, joinActor, unique, uniqueShort, require } from '../Support/scenario-support';

export async function runStE1(): Promise<void> {
  const actorId = uniqueShort('e1');
  const spotRid = unique('spot-bound-transfer');
  await createSpot(nodeB, spotRid);
  const source = await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 91);
  const transferId = uniqueShort('transfer');
  const connector = await connectAndBind(options.sessionAStreamEndpoint, 'ST-E1', source, transferId);
  try {
    await assertBoundPush(connector, nodeA, actorId, 'ST-E1', 'before-transfer', 'actor-a');
    require((await joinActor(nodeA, actorId, { scenario: 'ST-E1', targetSpotRid: spotRid, transferId })).accepted, 'ST-E1 join failed.');
    await assertHttpBoundPush(connector, nodeB, actorId, 'ST-E1', 'after-transfer', 'actor-b');
  } finally {
    await connector.close();
  }
}
