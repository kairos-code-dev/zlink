import type { TimerStartMsg } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import { startTimer, stopTimers, waitForEvidence } from './yd-c1-timer-isolation-scenario';

export async function runYdC2(client: ZlinkStreamConnector, spotRid: string): Promise<string> {
  const requestId = `YD-C2-${uniqueId()}`;
  await startTimer(client, spotRid, {
    requestId,
    timerName: `${requestId}-same`,
    mode: 'yield-then-next',
    periodMs: 50,
    delayMs: 350
  } satisfies TimerStartMsg);
  const evidence = await waitForEvidence(client, requestId, 'timer-next-completed');
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'timer-yield-started',
    'timer-yield-released',
    'timer-yield-resumed',
    'timer-yield-completed',
    'timer-next-started',
    'timer-next-completed'
  ], 'YD-C2 marker order mismatch.');
  await stopTimers(client, spotRid, requestId);
  console.log('scenario YD-C2 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
