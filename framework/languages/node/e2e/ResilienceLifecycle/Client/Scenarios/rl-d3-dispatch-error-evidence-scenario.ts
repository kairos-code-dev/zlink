import type { RequestFailureResult } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import { profileRequest } from '../Support/resilience-helpers';
import { ensure } from '../Support/scenario-assert';

export async function runRlD3(options: ClientOptions): Promise<void> {
  const failed = await postJson<RequestFailureResult>(
    options.consumerUrl,
    '/profile/missing-request',
    profileRequest('rl-d3-missing')
  );
  ensure(failed.failed, 'RL-D3 expected missing request handler failure.');

  const line = await waitForDispatchError(options);
  ensure(line.includes('packet=MissingProfileRequest'), 'RL-D3 dispatch-error evidence did not include MissingProfileRequest.');

  console.log('scenario RL-D3 passed');
}

async function waitForDispatchError(options: ClientOptions): Promise<string> {
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    const snapshots = await Promise.allSettled([
      getJson<string[]>(options.providerAUrl, '/evidence'),
      getJson<string[]>(options.providerBUrl, '/evidence')
    ]);
    for (const snapshot of snapshots) {
      if (snapshot.status !== 'fulfilled') {
        continue;
      }
      const line = snapshot.value.find((entry) =>
        entry.includes('dispatch-error|') && entry.includes('packet=MissingProfileRequest'));
      if (line !== undefined) {
        return line;
      }
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  throw new Error('RL-D3 dispatch-error evidence timed out.');
}
