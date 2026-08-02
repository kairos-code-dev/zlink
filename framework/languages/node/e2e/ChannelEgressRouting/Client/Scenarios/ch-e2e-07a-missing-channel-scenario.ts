// CH-E2E-07A: 등록하지 않은 ChannelName은 public NotFound로 종료한다.
import { assert, postJson } from '../Support/scenario-support';
import type { ClientOptions } from '../Support/client-options';

export async function runCh07A(options: ClientOptions): Promise<void> {
  const result = await postJson<{ succeeded: boolean; error?: string }>(options.sessionUrl, '/request', { channel: 'missing.channel', id: `ch-07a-${Date.now()}` });
  assert.equal(result.succeeded, false);
  assert.equal(result.error, 'NotFound');
}
