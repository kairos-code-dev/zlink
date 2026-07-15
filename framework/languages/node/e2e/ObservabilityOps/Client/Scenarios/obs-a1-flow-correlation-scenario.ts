// OBS-A1: flow가 STREAM→actor→room-spot을 관통 시나리오를 검증한다.
import {
  ObservabilityOpsNames,
  assertBoundPush,
  connectAndBind,
  createActor,
  createSpot,
  joinActor,
  nodeA,
  options,
  require,
  session,
  unique
} from '../Support/scenario-support.js';
import { waitForFlow } from '../Support/observability-support.js';

export async function runObsA1(): Promise<void> {
  const actorId = unique('obs-a1-actor');
  const spotRid = unique('obs-a1-room');
  await createSpot(nodeA, spotRid);
  const actor = await createActor(nodeA, actorId, ObservabilityOpsNames.actorTypeStateful, 1);
  require((await joinActor(nodeA, actorId, { scenario: 'OBS-A1', targetSpotRid: spotRid })).accepted,
    'OBS-A1 actor did not join the room Spot.');
  const connector = await connectAndBind(options.sessionAStreamEndpoint, 'OBS-A1', actor, unique('flow-bind'));
  try {
    await assertBoundPush(connector, nodeA, actorId, 'OBS-A1', 'flow-through-room', 'play-a');
  } finally {
    await connector.close();
  }
  const flow = await waitForFlow([session, nodeA], ObservabilityOpsNames.packetBoundPush);
  require(flow.length === 36, 'OBS-A1 did not preserve a UUIDv7 flow across roles.');
}
