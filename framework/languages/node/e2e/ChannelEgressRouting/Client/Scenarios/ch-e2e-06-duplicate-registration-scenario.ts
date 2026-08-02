// CH-E2E-06: 중복 egress registration은 정상 readiness 전에 configuration error가 된다.
import { assert, getJson } from '../Support/scenario-support';
import type { ClientOptions } from '../Support/client-options';

export async function runCh06(options: ClientOptions): Promise<void> {
  await assert.rejects(
    () => getJson(options.invalidUrl, '/health'),
    /fetch failed|ECONNREFUSED|socket hang up|other side closed/i
  );
}
