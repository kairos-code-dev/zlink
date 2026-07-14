import type { ClientOptions } from './client-options';
import { getJson } from './http-client';

export async function readProviderEvidence(options: ClientOptions): Promise<readonly string[]> {
  const snapshots = await Promise.allSettled([
    getJson<string[]>(options.providerAUrl, '/evidence'),
    getJson<string[]>(options.providerBUrl, '/evidence')
  ]);
  return snapshots.flatMap((snapshot) => snapshot.status === 'fulfilled' ? snapshot.value : []);
}

export function readProviderEvidenceFiles(options: ClientOptions): readonly string[] {
  return fs.readdirSync(options.logDir)
    .filter((name) => /^api-.*\.evidence\.log$/.test(name))
    .flatMap((name) => {
      try {
        return fs.readFileSync(path.join(options.logDir, name), 'utf8')
          .split(/\r?\n/)
          .filter((line) => line.length > 0);
      } catch {
        return [];
      }
    });
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
import fs from 'node:fs';
import path from 'node:path';
