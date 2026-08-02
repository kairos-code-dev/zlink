// CH-E2E-10: ClientServer one-way send는 reply payload 없이 handler evidence 하나를 만든다.
import { assert, postJson, waitForEvidence } from '../Support/scenario-support';
import type { ClientOptions } from '../Support/client-options';

export async function runCh10(options: ClientOptions): Promise<void> {
  const id = `ch-10-${Date.now()}`;
  const result = await postJson<{ succeeded: boolean; error?: string }>(options.workflowCallerUrl, '/send', { channel: 'workflow.command', id });
  assert.equal(result.succeeded, true, result.error);
  const evidence = await Promise.any([waitForEvidence(options.workflowAUrl, `id=${id}`), waitForEvidence(options.workflowBUrl, `id=${id}`)]);
  assert.equal(evidence.filter((entry) => entry.includes(`id=${id}`)).length, 1);
}
