// OBS-A4: workflow fanout branches share a flow and a timer callback starts a timer-origin flow.
import { post, require, unique, workflowA, workflowB } from '../Support/scenario-support.js';
import { readFlowLog, waitFor, waitForFlow } from '../Support/observability-support.js';
import type { ActorEvidence, WorkflowApplyRes } from '../../Shared/messages.js';

export async function runObsA4(): Promise<void> {
  const orderId = unique('obs-a4-order');
  const result = await post<WorkflowApplyRes>(workflowA, '/workflows', { orderId, value: 1 });
  require(result.value === 2, 'OBS-A4 workflow state did not apply the event.');
  for (const client of [workflowA, workflowB]) {
    await waitFor(async () => await client.get('/evidence').fetch<ActorEvidence[]>(),
      (entries) => entries.some((entry) => entry.scenario === 'projection' && entry.actorId === orderId),
      `OBS-A4 projection was not received by ${client === workflowA ? 'workflow-a' : 'workflow-b'}`);
  }
  await waitForFlow([workflowA, workflowB], 'WorkflowProjected');
  const timerLog = await waitFor(async () => await readFlowLog(workflowA),
    (value) => value.includes('origin=Timer'), 'OBS-A4 timer did not originate a new flow');
  require(timerLog.includes('flow='), 'OBS-A4 timer line did not include a flow.');
}
