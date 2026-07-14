import type { ClientOptions } from './client-options';
import { getJson } from './http-client';

export async function readProviderEvidence(options: ClientOptions): Promise<readonly string[]> {
  const snapshots = await Promise.allSettled([
    getJson<string[]>(options.providerAUrl, '/evidence'),
    getJson<string[]>(options.providerBUrl, '/evidence')
  ]);
  return snapshots.flatMap((snapshot) => snapshot.status === 'fulfilled' ? snapshot.value : []);
}

export async function waitForProviderEvidenceLine(
  options: ClientOptions,
  predicate: (line: string) => boolean,
  failureMessage: string,
  timeoutMs = 15_000
): Promise<string> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const line = (await readProviderEvidence(options)).find(predicate);
    if (line !== undefined) return line;
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  throw new Error(failureMessage);
}
