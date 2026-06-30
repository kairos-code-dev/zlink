import type { ClientOptions } from '../Support/client-options';
import { DynamicClusterLauncher } from '../Support/dynamic-cluster-launcher';
import { getJson, postJson } from '../Support/http-client';
import { countNewEvidence, ensure, uniqueMarker } from '../Support/scenario-assert';
import type { ProfileReply } from '../../Shared/messages';

export async function runRmC7(options: ClientOptions): Promise<void> {
  const cluster = await DynamicClusterLauncher.start(options, 'rm-c7');
  try {
    const providerA = await cluster.startProvider('api-a-weighted', 'api-a', 75);
    const providerB = await cluster.startProvider('api-b-weighted', 'api-b', 25);
    const beforeA = await getJson<string[]>(providerA.httpUrl, '/evidence');
    const beforeB = await getJson<string[]>(providerB.httpUrl, '/evidence');
    const marker = uniqueMarker('rm-c7');
    const values = Array.from({ length: 240 }, (_, index) => `${marker}-${index}`);
    const replies: ProfileReply[] = [];

    for (const value of values) {
      replies.push(await postJson<ProfileReply>(providerA.httpUrl, '/profile/request', { value }));
    }

    ensure(replies.length === values.length, 'RM-C7 reply count mismatch.');
    ensure(
      replies.every((reply) => reply.providerRid === 'api-a' || reply.providerRid === 'api-b'),
      'RM-C7 reply provider mismatch.'
    );

    const apiAValues = replies.filter((reply) => reply.providerRid === 'api-a').map((reply) => reply.value);
    const apiBValues = replies.filter((reply) => reply.providerRid === 'api-b').map((reply) => reply.value);
    ensure(apiAValues.length > 0 && apiBValues.length > 0, 'RM-C7 expected both weighted providers.');

    const afterA = await postJson<string[]>(providerA.httpUrl, '/evidence/wait', {
      contains: apiAValues[apiAValues.length - 1],
      timeoutMilliseconds: 20000
    });
    const afterB = await postJson<string[]>(providerB.httpUrl, '/evidence/wait', {
      contains: apiBValues[apiBValues.length - 1],
      timeoutMilliseconds: 20000
    });
    const a = countNewEvidence(afterA, beforeA, 'profile-request|rid=api-a', marker);
    const b = countNewEvidence(afterB, beforeB, 'profile-request|rid=api-b', marker);
    ensure(
      a === apiAValues.length &&
        b === apiBValues.length &&
        a + b === values.length &&
        a > b * 2,
      'RM-C7 weighted provider counts did not favor api-a.'
    );
    console.log('scenario RM-C7 passed');
  } finally {
    await cluster.close();
  }
}
