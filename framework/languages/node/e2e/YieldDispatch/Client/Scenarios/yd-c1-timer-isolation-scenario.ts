import type {
  EnsureSpotRes,
  EnsureSpotReq,
  TimerStartMsg,
  TimerStopMsg,
  YieldEvidenceRes,
  YieldEvidenceWaitReq
} from '../../Shared/messages';
import { YieldDispatchNames } from '../../Shared/messages';
import { containsRequestMarkersInOrder, ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';

export interface YieldTimerScenarioContext {
  readonly spotRid: string;
  readonly requestId: string;
}

export async function runYdC1(client: ZlinkStreamConnector): Promise<YieldTimerScenarioContext> {
  const spotRid = `yield-timer-${uniqueId()}`;
  const spot = decodeStreamReply<EnsureSpotRes>(await client
    .request({ spotRid } satisfies EnsureSpotReq)
    .packetName('EnsureSpotReq')
    .timeout(30000)
    .submit());
  ensure(spot.spotRid === spotRid, 'YD-C timer spot creation mismatch.');

  const requestId = `YD-C1-${uniqueId()}`;
  await client
    .send({ requestId, timerName: `${requestId}-yield`, mode: 'yield-on-first', periodMs: 50, delayMs: 350 } satisfies TimerStartMsg)
    .packetName('TimerStartMsg')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await waitForEvidence(client, requestId, 'timer-yield-released');
  await client
    .send({ requestId, timerName: `${requestId}-fast`, mode: 'fast', periodMs: 50, delayMs: 0 } satisfies TimerStartMsg)
    .packetName('TimerStartMsg')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();
  await waitForEvidence(client, requestId, 'timer-fast-completed');
  const evidence = await waitForEvidence(client, requestId, 'timer-yield-completed');
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'timer-yield-started',
    'timer-yield-released',
    'timer-fast-started',
    'timer-fast-completed',
    'timer-yield-resumed',
    'timer-yield-completed'
  ], 'YD-C1 marker order mismatch.');
  await stopTimers(client, spotRid, requestId);
  console.log('scenario YD-C1 passed');
  return { spotRid, requestId };
}

export async function waitForEvidence(
  client: ZlinkStreamConnector,
  requestId: string,
  marker: string
): Promise<YieldEvidenceRes> {
  return decodeStreamReply<YieldEvidenceRes>(await client
    .request({ requestId, marker } satisfies YieldEvidenceWaitReq)
    .packetName('YieldEvidenceWaitReq')
    .metadata(YieldDispatchNames.targetNodeRidMetadata, 'play-a')
    .timeout(30000)
    .submit());
}

export async function stopTimers(client: ZlinkStreamConnector, spotRid: string, requestId: string): Promise<void> {
  await client
    .send({ requestId } satisfies TimerStopMsg)
    .packetName('TimerStopMsg')
    .metadata(YieldDispatchNames.spotRidMetadata, spotRid)
    .submit();
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
