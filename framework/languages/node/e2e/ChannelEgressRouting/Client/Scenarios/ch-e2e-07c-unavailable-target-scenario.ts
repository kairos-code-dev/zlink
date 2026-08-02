// CH-E2E-07C: Known but disconnected target은 Unavailable로 종료한다.
import { assert, postJson } from '../Support/scenario-support';
import type { ClientOptions } from '../Support/client-options';

export async function runCh07C(options: ClientOptions): Promise<void> {
  const result = await postJson<{ succeeded: boolean; error?: string }>(options.workflowCallerUrl, '/request', { channel: 'workflow.command', id: `ch-07c-${Date.now()}` });
  assert.equal(result.succeeded, false);
  assert.equal(result.error, 'Unavailable');
}
