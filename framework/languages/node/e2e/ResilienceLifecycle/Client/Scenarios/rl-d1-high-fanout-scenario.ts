import type { ProfileReply } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { profileRequest } from '../Support/resilience-helpers';
import { ensure } from '../Support/scenario-assert';

export async function runRlD1(options: ClientOptions): Promise<void> {
  const replies = await Promise.all(
    Array.from({ length: 120 }, (_unused, index) => postJson<ProfileReply>(
      options.consumerUrl,
      '/profile/request',
      profileRequest(`rl-d1-${index}`)
    ))
  );
  ensure(
    replies.length === 120 && replies.every((reply) => reply.value === 'profile:fast'),
    'RL-D1 high request fanout did not complete.'
  );

  const evidence = await Promise.any([
    postJson<string[]>(options.providerAUrl, '/evidence/wait', { contains: 'marker=rl-d1-' }),
    postJson<string[]>(options.providerBUrl, '/evidence/wait', { contains: 'marker=rl-d1-' })
  ]);
  ensure(
    evidence.some((line) => line.includes('marker=rl-d1-')),
    'RL-D1 did not record expected evidence.'
  );

  console.log('scenario RL-D1 passed');
}
