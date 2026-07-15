// ATD-C2: 같은 timer의 await 중 다음 tick 재진입 방지 검증한다.
import type { TimerStartMsg } from '../../Shared/messages';
import { containsRequestMarkersInOrder } from '../Support/scenario-assert';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import { startTimer, stopTimers, waitForEvidence } from './atd-c1-timer-isolation-scenario';

export async function runYdC2(client: ZlinkStreamConnector, spotRid: string): Promise<string> {
  const requestId = `ATD-C2-${uniqueId()}`;
  await startTimer(client, spotRid, {
    requestId,
    timerName: `${requestId}-same`,
    mode: 'await-then-next',
    periodMs: 50,
    delayMs: 350
  } satisfies TimerStartMsg);
  const evidence = await waitForEvidence(client, requestId, 'timer-next-completed');
  containsRequestMarkersInOrder(evidence.evidence, requestId, [
    'timer-await-started',
    'timer-await-released',
    'timer-await-resumed',
    'timer-await-completed',
    'timer-next-started',
    'timer-next-completed'
  ], 'ATD-C2 marker order mismatch.');
  await stopTimers(client, spotRid, requestId);
  console.log('scenario ATD-C2 passed');
  return requestId;
}

function uniqueId(): string {
  return `${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
}
