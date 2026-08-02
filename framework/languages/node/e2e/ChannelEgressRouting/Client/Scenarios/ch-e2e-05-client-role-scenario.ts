// CH-E2E-05: Client role이 없는 Server-only process의 outbound request를 거부한다.
import { assert, postJson } from '../Support/scenario-support';
import type { ClientOptions } from '../Support/client-options';

export async function runCh05(options: ClientOptions): Promise<void> {
  const result = await postJson<{ succeeded: boolean; error?: string }>(options.workflowAUrl, '/request', { channel: 'workflow.command', id: `ch-05-${Date.now()}` });
  assert.equal(result.succeeded, false);
  assert.equal(result.error, 'NotFound');
}
