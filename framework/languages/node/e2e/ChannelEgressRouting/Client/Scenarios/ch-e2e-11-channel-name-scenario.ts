// CH-E2E-11: Caller가 physical route 정보 없이 ChannelName만 사용한다.
import { assert, postJson, waitForEvidence } from '../Support/scenario-support';
import type { ClientOptions } from '../Support/client-options';

export async function runCh11(options: ClientOptions): Promise<void> {
  const id = `ch-11-${Date.now()}`;
  const request = await postJson<{ succeeded: boolean; reply?: { role: string } }>(options.sessionUrl, '/request', { channel: 'game.api', id });
  assert.equal(request.succeeded, true);
  assert.ok(request.reply?.role === 'api' || request.reply?.role === 'api-a' || request.reply?.role === 'api-b');
  const send = await postJson<{ succeeded: boolean }>(options.sessionUrl, '/send', { channel: 'game.api', id: `${id}-send` });
  assert.equal(send.succeeded, true);
  await Promise.race([waitForEvidence(options.apiAUrl, `id=${id}-send`), waitForEvidence(options.apiBUrl, `id=${id}-send`)]);
}
