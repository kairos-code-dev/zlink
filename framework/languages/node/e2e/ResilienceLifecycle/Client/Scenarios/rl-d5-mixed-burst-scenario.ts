import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import { profileReq } from '../Support/resilience-helpers';
import { ensure } from '../Support/scenario-assert';

export async function runRlD5(options: ClientOptions): Promise<void> {
  const requestTasks = Array.from({ length: 60 }, (_unused, index) => postJson<ProfileRes>(
    options.consumerUrl,
    '/profile/request',
    profileReq(`rl-d5-req-${index}`)
  ));
  const sendTasks = Array.from({ length: 60 }, (_unused, index) => postJson(
    options.consumerUrl,
    '/profile/command',
    { commandId: `rl-d5-cmd-${index}` }
  ));

  const replies = await Promise.all(requestTasks);
  await Promise.all(sendTasks);
  ensure(replies.every((reply) => reply.value === 'profile:fast'), 'RL-D5 mixed workload replies were invalid.');

  await waitForEitherEvidence(options, 'marker=rl-d5-req-', 'RL-D5 evidence missing for marker=rl-d5-req-.');
  await waitForEitherEvidence(options, 'marker=rl-d5-cmd-', 'RL-D5 evidence missing for marker=rl-d5-cmd-.');

  console.log('scenario RL-D5 passed');
}

async function waitForEitherEvidence(options: ClientOptions, contains: string, message: string): Promise<void> {
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    const snapshots = await Promise.allSettled([
      getJson<string[]>(options.providerAUrl, '/evidence'),
      getJson<string[]>(options.providerBUrl, '/evidence')
    ]);
    if (snapshots.some((snapshot) =>
      snapshot.status === 'fulfilled' && snapshot.value.some((line) => line.includes(contains)))) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  ensure(false, message);
}
