import type { RequestFailureRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { waitForProviderEvidenceLine } from '../Support/provider-evidence';
import { profileReq } from '../Support/resilience-helpers';
import { ensure } from '../Support/scenario-assert';

export async function runRlD4(options: ClientOptions): Promise<void> {
  const failed = await postJson<RequestFailureRes>(
    options.consumerUrl,
    '/profile/missing-request',
    profileReq('rl-d4-missing')
  );
  ensure(failed.failed, 'RL-D4 expected public failure for missing request handler.');
  ensure(failed.failureType === 'Error', 'RL-D4 decoded errorCode did not preserve Error.');
  ensure(
    failed.failureMessage.includes('request handler is registered'),
    'RL-D4 decoded errorMessage did not preserve the missing-handler message.'
  );

  const line = await waitForProviderEvidenceLine(
    options,
    (entry) => entry.includes('dispatch-error|') && entry.includes('packet=MissingProfileReq'),
    'RL-D4 dispatch-error evidence timed out.'
  );
  ensure(
    line.includes('dispatch-error|')
      && line.includes('reason=handlerMissing')
      && line.includes('action=replyError')
      && line.includes('packet=MissingProfileReq'),
    'RL-D4 dispatch-error marker missing.'
  );

  const followUp = await postJson<{ readonly value: string }>(
    options.consumerUrl,
    '/profile/request',
    profileReq('rl-d4-follow-up')
  );
  ensure(followUp.value === 'profile:fast', 'RL-D4 success Response follow-up failed.');

  console.log('scenario RL-D4 passed');
}
