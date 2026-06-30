import type { ProfileReply, TimeoutRequestResult } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { profileRequest } from '../Support/resilience-helpers';
import { ensure } from '../Support/scenario-assert';

export async function runRlB1(options: ClientOptions): Promise<void> {
  const slowMarker = `rl-b1-slow-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const timeout = await postJson<TimeoutRequestResult>(
    options.consumerUrl,
    '/profile/request/timeout/100',
    profileRequestWithValue('slow', slowMarker)
  );
  ensure(timeout.status === 408 && timeout.timedOut, 'RL-B1 expected the slow request to time out.');

  const followUpMarker = `rl-b1-follow-up-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const followUp = await postJson<ProfileReply>(options.consumerUrl, '/profile/request', profileRequest(followUpMarker));
  ensure(followUp.value === 'profile:fast', 'RL-B1 follow-up request failed after timeout.');

  const evidence = await Promise.any([
    postJson<string[]>(options.providerAUrl, '/evidence/wait', {
      contains: `profile-request|rid=api-a|value=slow|marker=${slowMarker}`,
      timeoutMilliseconds: 15000
    }),
    postJson<string[]>(options.providerBUrl, '/evidence/wait', {
      contains: `profile-request|rid=api-b|value=slow|marker=${slowMarker}`,
      timeoutMilliseconds: 15000
    })
  ]);
  ensure(
    evidence.some((line) => line.includes('profile-request|') && line.includes(`marker=${slowMarker}`)),
    'RL-B1 slow request completion evidence missing.'
  );

  const later = await postJson<ProfileReply>(
    options.consumerUrl,
    '/profile/request',
    profileRequest(`rl-b1-later-${Date.now()}-${Math.random().toString(16).slice(2)}`)
  );
  ensure(later.value === 'profile:fast', 'RL-B1 later request failed after slow completion.');

  console.log('scenario RL-B1 passed');
}

function profileRequestWithValue(value: string, marker: string): { readonly value: string; readonly marker: string } {
  return { value, marker };
}
