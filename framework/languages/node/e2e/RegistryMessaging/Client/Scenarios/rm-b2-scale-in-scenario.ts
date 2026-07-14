import type { ClientOptions } from '../Support/client-options';
import { DynamicClusterLauncher } from '../Support/dynamic-cluster-launcher';
import { getJson, postJson } from '../Support/http-client';
import { countNewEvidence, ensure, uniqueMarker } from '../Support/scenario-assert';
import type { ProfileRes } from '../../Shared/messages';

export async function runRmB2(options: ClientOptions): Promise<void> {
  const cluster = await DynamicClusterLauncher.start(options, 'rm-b2');
  try {
    const providerA = await cluster.startProvider('api-a', 'api-a');
    const providerB = await cluster.startProvider('api-b', 'api-b');
    const consumer = await cluster.startConsumer('rm-b2-consumer');
    await cluster.waitForProviders([providerA.channelEndpoint, providerB.channelEndpoint]);
    const markerBefore = uniqueMarker('rm-b2-before');
    const beforeA = await getJson<string[]>(providerA.httpUrl, '/evidence');
    const beforeB = await getJson<string[]>(providerB.httpUrl, '/evidence');
    const repliesBefore: Array<{ requestValue: string; reply: ProfileRes }> = [];
    for (let i = 0; i < 40; i += 1) {
      const requestValue = `${markerBefore}-${i}`;
      repliesBefore.push({
        requestValue,
        reply: await postJson<ProfileRes>(consumer.httpUrl, '/profile/request', { value: requestValue })
      });
    }
    const apiABeforeValues = repliesBefore.filter((entry) => entry.reply.providerRid === 'api-a').map((entry) => entry.requestValue);
    const apiBBeforeValues = repliesBefore.filter((entry) => entry.reply.providerRid === 'api-b').map((entry) => entry.requestValue);
    ensure(apiABeforeValues.length > 0 && apiBBeforeValues.length > 0, 'RM-B2 expected both providers before scale-in.');
    const scaleOutA = await postJson<string[]>(providerA.httpUrl, '/evidence/wait', { contains: apiABeforeValues[apiABeforeValues.length - 1] });
    const scaleOutB = await postJson<string[]>(providerB.httpUrl, '/evidence/wait', { contains: apiBBeforeValues[apiBBeforeValues.length - 1] });
    ensure(
      countNewEvidence(scaleOutA, beforeA, 'profile-request|rid=api-a', markerBefore)
      + countNewEvidence(scaleOutB, beforeB, 'profile-request|rid=api-b', markerBefore) === 40,
      'RM-B2 expected both providers before scale-in.'
    );

    const traffic = sendContinuously(consumer.httpUrl, uniqueMarker('rm-b2-during'), 40);
    await new Promise((resolve) => setTimeout(resolve, 100));
    await cluster.stop(providerB);
    const duringScaleIn = await traffic;
    const rejected = duringScaleIn.flatMap((outcome) =>
      outcome.status === 'rejected' ? [String(outcome.reason)] : []);
    ensure(
      duringScaleIn.every(isExpectedScaleInOutcome),
      `RM-B2 in-flight request did not finish with a reply or submit timeout: ${rejected.join(' | ')}`
    );
    await cluster.waitForSingleProvider('api-a', providerA.channelEndpoint);
    const beforeAfterA = await getJson<string[]>(providerA.httpUrl, '/evidence');
    const markerAfter = uniqueMarker('rm-b2-after');
    const repliesAfter: ProfileRes[] = [];
    for (let i = 0; i < 20; i += 1) {
      repliesAfter.push(await postJson<ProfileRes>(consumer.httpUrl, '/profile/request', { value: `${markerAfter}-${i}` }));
    }
    ensure(repliesAfter.every((reply) => reply.providerRid === 'api-a'), 'RM-B2 after scale-in should reach api-a only.');
    const afterA = await postJson<string[]>(providerA.httpUrl, '/evidence/wait', { contains: `${markerAfter}-19` });
    const a = countNewEvidence(afterA, beforeAfterA, 'profile-request|rid=api-a', markerAfter);
    ensure(a === 20, 'RM-B2 expected only api-a after scale-in.');
    console.log('scenario RM-B2 passed');
  } finally {
    await cluster.close();
  }
}

function isExpectedScaleInOutcome(outcome: PromiseSettledResult<ProfileRes>): boolean {
  if (outcome.status === 'fulfilled') {
    return outcome.value.providerRid === 'api-a' || outcome.value.providerRid === 'api-b';
  }
  return String(outcome.reason).includes('ZLink async submit timed out.');
}

async function sendContinuously(
  consumerUrl: string,
  marker: string,
  count: number
): Promise<PromiseSettledResult<ProfileRes>[]> {
  const requests: Promise<ProfileRes>[] = [];
  for (let i = 0; i < count; i += 1) {
    requests.push(postJson<ProfileRes>(consumerUrl, '/profile/request', { value: `${marker}-${i}` }));
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
  return await Promise.allSettled(requests);
}
