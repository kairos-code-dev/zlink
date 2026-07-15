// ATD-C3: actor와 timer await 사이의 상호 진행 검증한다.
import type {
  ActorFastReq,
  ActorAwaitReq,
  TimerStartMsg,
  AwaitEvidenceRes,
  AwaitEvidenceReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type { AwaitActorScenarioContext } from './atd-b1-other-actor-progress-scenario';
import { startTimer, stopTimers, waitForEvidence } from './atd-c1-timer-isolation-scenario';

export async function runYdC3(
  client: ZlinkStreamConnector,
  evidenceClient: ZlinkStreamConnector,
  actors: AwaitActorScenarioContext
): Promise<void> {
  const actorThenTimer = `ATD-C3A-${uniqueId()}`;
  const actorAwait = client
    .request({ requestId: actorThenTimer, delayMs: 350 } satisfies ActorAwaitReq)
    .packetName('ActorAwaitReq')
    .metadata(AutomaticTurnDispatchNames.actorIdMetadata, actors.actorA)
    .timeout(30000)
    .submit();
  await new Promise((resolve) => setTimeout(resolve, 75));
  await startTimer(evidenceClient, actors.spotRid, {
    requestId: actorThenTimer,
    timerName: `${actorThenTimer}-fast`,
    mode: 'fast',
    periodMs: 50,
    delayMs: 0
  } satisfies TimerStartMsg);
  await waitForEvidence(evidenceClient, actorThenTimer, 'timer-fast-completed');
  await stopTimers(evidenceClient, actors.spotRid, actorThenTimer);
  await actorAwait;

  const actorThenTimerEvidence = await evidenceClient
    .request({ requestId: actorThenTimer } satisfies AwaitEvidenceReq)
    .packetName('AwaitEvidenceReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
  containsRequestMarkersInOrder(actorThenTimerEvidence.evidence, actorThenTimer, [
    'actor-await-started',
    'actor-await-released',
    'timer-fast-started',
    'timer-fast-completed',
    'actor-await-resumed',
    'actor-await-completed'
  ], 'ATD-C3 actor-then-timer marker order mismatch.');

  const timerThenActor = `ATD-C3B-${uniqueId()}`;
  await startTimer(client, actors.spotRid, {
    requestId: timerThenActor,
    timerName: `${timerThenActor}-await`,
    mode: 'await-on-first',
    periodMs: 50,
    delayMs: 350
  } satisfies TimerStartMsg);
  await waitForEvidence(client, timerThenActor, 'timer-await-released');
  await client
    .request({ requestId: timerThenActor, marker: 'c3-actor-fast' } satisfies ActorFastReq)
    .packetName('ActorFastReq')
    .metadata(AutomaticTurnDispatchNames.actorIdMetadata, actors.actorB)
    .timeout(30000)
    .submit();
  const timerThenActorEvidence = await waitForEvidence(client, timerThenActor, 'timer-await-completed');
  await stopTimers(client, actors.spotRid, timerThenActor);

  containsRequestMarkersInOrder(timerThenActorEvidence.evidence, timerThenActor, [
    'timer-await-started',
    'timer-await-released',
    'actor-fast-started',
    'actor-fast-completed',
    'timer-await-resumed',
    'timer-await-completed'
  ], 'ATD-C3 timer-then-actor marker order mismatch.');
  console.log('scenario ATD-C3 passed');
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
