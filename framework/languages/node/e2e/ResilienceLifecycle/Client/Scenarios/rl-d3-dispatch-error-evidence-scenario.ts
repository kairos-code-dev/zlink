// RL-D3: 로그 marker 관측 시나리오를 검증한다.
import type { RequestFailureRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { waitForProviderEvidenceLine } from '../Support/provider-evidence';
import { profileReq } from '../Support/resilience-helpers';
import { ensure } from '../Support/scenario-assert';

export async function runRlD3(options: ClientOptions): Promise<void> {
  const failed = await postJson<RequestFailureRes>(
    options.consumerUrl,
    '/profile/missing-request',
    profileReq('rl-d3-missing')
  );
  ensure(failed.failed, 'RL-D3 expected missing request handler failure.');

  const line = await waitForProviderEvidenceLine(
    options,
    (entry) => entry.includes('dispatch-error|') && entry.includes('packet=MissingProfileReq'),
    'RL-D3 dispatch-error evidence timed out.'
  );
  ensure(line.includes('packet=MissingProfileReq'), 'RL-D3 dispatch-error evidence did not include MissingProfileReq.');

  console.log('scenario RL-D3 passed');
}
