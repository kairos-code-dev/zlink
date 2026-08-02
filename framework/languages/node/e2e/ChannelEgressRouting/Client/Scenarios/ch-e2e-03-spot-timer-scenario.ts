// CH-E2E-03: Instance Spot handler와 실제 timer가 ClientServer request를 기다린다.
import { assert, postJson, waitForEvidence } from '../Support/scenario-support';
import type { ClientOptions } from '../Support/client-options';

export async function runCh03(options: ClientOptions): Promise<void> {
  const id = `ch-03-${Date.now()}`;
  const result = await postJson<{ succeeded: boolean; error?: string }>(options.spotCallerUrl, '/spot/workflow', { spotId: `ch-03-spot-${Date.now()}`, id });
  assert.equal(result.succeeded, true, result.error);
  const evidence = await waitForEvidence(options.playUrl, 'spot-timer-end');
  const line = evidence.find((entry) => entry.includes('spot-timer-end')) ?? '';
  assert.match(line, /sequence=handler-start,workflow-reply,handler-end,timer-start,workflow-reply,timer-end/);
}
