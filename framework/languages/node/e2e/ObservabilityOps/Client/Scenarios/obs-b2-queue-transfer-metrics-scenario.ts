// OBS-B2: an actor transfer and user Spot work emit queue and transfer instruments.
import {
  ObservabilityOpsNames,
  createActor,
  createSpot,
  joinActor,
  nodeA,
  nodeB,
  probeActor,
  require,
  unique
} from '../Support/scenario-support.js';
import { metric, metrics, waitFor } from '../Support/observability-support.js';

export async function runObsB2(): Promise<void> {
  const actorId = unique('obs-b2-actor');
  const spotRid = unique('obs-b2-room');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, ObservabilityOpsNames.actorTypeStateful, 2);
  require((await joinActor(nodeA, actorId, { scenario: 'OBS-B2', targetSpotRid: spotRid })).accepted,
    'OBS-B2 actor transfer failed.');
  require((await probeActor(nodeB, actorId, 'OBS-B2', 'after-transfer')).nodeRid === 'play-b',
    'OBS-B2 actor did not reach play-b.');
  const source = await waitFor(async () => await metrics(nodeA),
    (values) => values.some((value) => value.name === 'zlink.actor.transfers' && value.value >= 1),
    'OBS-B2 actor transfer metric was not recorded');
  require(metric(source, 'zlink.actor.transfers').value >= 1, 'OBS-B2 transfer counter mismatch.');
  metric(source, 'zlink.actor.transfer.duration');
  metric(source, 'zlink.actor.transfer.pending_requests.count');
  const target = await metrics(nodeB);
  metric(target, 'zlink.spot.queue.wait.duration', (value) => value.tags.kind === 'user');
  metric(target, 'zlink.spot.queue.depth', (value) => value.tags.kind === 'user');
}
