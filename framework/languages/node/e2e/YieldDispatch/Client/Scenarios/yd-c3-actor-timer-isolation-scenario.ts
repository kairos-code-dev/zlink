import type {
  ActorFastReq,
  ActorYieldReq,
  TimerStartMsg,
  YieldEvidenceRes,
  YieldEvidenceReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type { YieldActorScenarioContext } from './yd-b1-other-actor-progress-scenario';
import { stopTimers, waitForEvidence } from './yd-c1-timer-isolation-scenario';

export async function runYdC3(
  client: ZlinkStreamConnector,
  evidenceClient: ZlinkStreamConnector,
  actors: YieldActorScenarioContext
): Promise<void> {
  const actorThenTimer = `YD-C3A-${uniqueId()}`;
  const actorYield = client
    .request({ requestId: actorThenTimer, delayMs: 350 } satisfies ActorYieldReq)
    .packetName('ActorYieldReq')
    .metadata(YieldDispatchNames.actorIdMetadata, actors.actorA)
    .timeout(30000)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  await evidenceClient
    .send({ requestId: actorThenTimer, timerName: `${actorThenTimer}-fast`, mode: 'fast', periodMs: 50, delayMs: 0 } satisfies TimerStartMsg)
    .packetName('TimerStartMsg')
    .metadata(YieldDispatchNames.spotRidMetadata, actors.spotRid)
    .submit();
  await waitForEvidence(evidenceClient, actorThenTimer, 'timer-fast-completed');
  await stopTimers(evidenceClient, actors.spotRid, actorThenTimer);
  await actorYield;

  const actorThenTimerEvidence = decodeStreamReply<YieldEvidenceRes>(await evidenceClient
    .request({ requestId: actorThenTimer } satisfies YieldEvidenceReq)
    .packetName('YieldEvidenceReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit());
  containsRequestMarkersInOrder(actorThenTimerEvidence.evidence, actorThenTimer, [
    'actor-yield-started',
    'actor-yield-released',
    'timer-fast-started',
    'timer-fast-completed',
    'actor-yield-resumed',
    'actor-yield-completed'
  ], 'YD-C3 actor-then-timer marker order mismatch.');

  const timerThenActor = `YD-C3B-${uniqueId()}`;
  await client
    .send({ requestId: timerThenActor, timerName: `${timerThenActor}-yield`, mode: 'yield-on-first', periodMs: 50, delayMs: 350 } satisfies TimerStartMsg)
    .packetName('TimerStartMsg')
    .metadata(YieldDispatchNames.spotRidMetadata, actors.spotRid)
    .submit();
  await waitForEvidence(client, timerThenActor, 'timer-yield-released');
  await client
    .request({ requestId: timerThenActor, marker: 'c3-actor-fast' } satisfies ActorFastReq)
    .packetName('ActorFastReq')
    .metadata(YieldDispatchNames.actorIdMetadata, actors.actorB)
    .timeout(30000)
    .submit();
  const timerThenActorEvidence = await waitForEvidence(client, timerThenActor, 'timer-yield-completed');
  await stopTimers(client, actors.spotRid, timerThenActor);

  containsRequestMarkersInOrder(timerThenActorEvidence.evidence, timerThenActor, [
    'timer-yield-started',
    'timer-yield-released',
    'actor-fast-started',
    'actor-fast-completed',
    'timer-yield-resumed',
    'timer-yield-completed'
  ], 'YD-C3 timer-then-actor marker order mismatch.');
  console.log('scenario YD-C3 passed');
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
