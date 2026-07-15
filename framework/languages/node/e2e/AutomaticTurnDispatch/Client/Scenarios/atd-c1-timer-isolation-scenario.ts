// ATD-C1: 한 timer의 await 중 다른 timer 진행 검증한다.
import type {
  EnsureSpotRes,
  EnsureSpotReq,
  TimerStartMsg,
  TimerStopMsg,
  AwaitEvidenceRes,
  AwaitEvidenceWaitReq
} from '../../Shared/messages';
import { AutomaticTurnDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export interface AwaitTimerScenarioContext {
  readonly spotRid: string;
  readonly requestId: string;
}

export async function runYdC1(client: ZlinkStreamConnector): Promise<AwaitTimerScenarioContext> {
  const spotRid = `await-timer-${uniqueId()}`;
  const spot = await client
    .request({ spotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .timeout(30000)
    .submit<EnsureSpotRes>();
  ensure(spot.spotRid === spotRid, 'ATD-C timer spot creation mismatch.');

  const requestId = `ATD-C1-${uniqueId()}`;
  await startTimer(client, spotRid, {
    requestId,
    timerName: `${requestId}-await`,
    mode: 'await-on-first',
    periodMs: 50,
    delayMs: 350
  });
  await waitForEvidence(client, requestId, 'timer-await-released');
  await startTimer(client, spotRid, {
    requestId,
    timerName: `${requestId}-fast`,
    mode: 'fast',
    periodMs: 50,
    delayMs: 0
  });
  await waitForEvidence(client, requestId, 'timer-fast-completed');
  const evidence = await waitForEvidence(client, requestId, 'timer-await-completed');
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'timer-await-started',
    'timer-await-released',
    'timer-fast-started',
    'timer-fast-completed',
    'timer-await-resumed',
    'timer-await-completed'
  ], 'ATD-C1 marker order mismatch.');
  await stopTimers(client, spotRid, requestId);
  console.log('scenario ATD-C1 passed');
  return { spotRid, requestId };
}

export async function waitForEvidence(
  client: ZlinkStreamConnector,
  requestId: string,
  marker: string
): Promise<AwaitEvidenceRes> {
  return await client
    .request({ requestId, marker } satisfies AwaitEvidenceWaitReq)
    .packetName('AwaitEvidenceWaitReq')
    .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit<AwaitEvidenceRes>();
}

export async function stopTimers(client: ZlinkStreamConnector, spotRid: string, requestId: string): Promise<void> {
  await client
    .request({ requestId } satisfies TimerStopMsg)
    .packetName('TimerStopMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .timeout(30000)
    .submit();
}

export async function startTimer(
  client: ZlinkStreamConnector,
  spotRid: string,
  request: TimerStartMsg
): Promise<void> {
  await client
    .request(request)
    .packetName('TimerStartMsg')
    .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
    .timeout(30000)
    .submit();
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
