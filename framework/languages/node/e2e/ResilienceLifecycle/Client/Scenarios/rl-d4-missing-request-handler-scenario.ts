import type { RequestFailureRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import { profileReq } from '../Support/resilience-helpers';
import { ensure } from '../Support/scenario-assert';

export async function runRlD4(options: ClientOptions): Promise<void> {
  const failed = await postJson<RequestFailureRes>(
    options.consumerUrl,
    '/profile/missing-request',
    profileReq('rl-d4-missing')
  );
  ensure(failed.failed, 'RL-D4 expected public failure for missing request handler.');

  const line = await waitForDispatchError(options);
  ensure(
    line.includes('dispatch-error|') && line.includes('packet=MissingProfileReq'),
    'RL-D4 dispatch-error marker missing.'
  );

  console.log('scenario RL-D4 passed');
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
        entry.includes('dispatch-error|') && entry.includes('packet=MissingProfileReq'));
      if (line !== undefined) {
        return line;
      }
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  throw new Error('RL-D4 dispatch-error evidence timed out.');
}
