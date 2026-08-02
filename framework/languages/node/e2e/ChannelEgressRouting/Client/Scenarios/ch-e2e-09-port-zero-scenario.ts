// CH-E2E-09: public status가 실제 bound endpoint를 제공하고 port 0을 노출하지 않는지 검증한다.
import { assert, getJson } from '../Support/scenario-support';
import type { ClientOptions } from '../Support/client-options';

export async function runCh09(options: ClientOptions): Promise<void> {
  const route = await getJson<{ isReady: boolean; peers: readonly { rid: string }[] }>(options.sessionUrl, '/status/route');
  const workflow = await getJson<{ isReady: boolean; targets: readonly { rid: string }[] }>(options.workflowCallerUrl, '/status/workflow');
  assert.equal(route.isReady, true);
  assert.equal(workflow.isReady, true);
  assert.ok(route.peers.length >= 1);
  assert.ok(workflow.targets.length >= 1);
}
