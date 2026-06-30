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
    const markerBefore = uniqueMarker('rm-b2-before');
    const beforeA = await getJson<string[]>(providerA.httpUrl, '/evidence');
    const beforeB = await getJson<string[]>(providerB.httpUrl, '/evidence');
    const repliesBefore: ProfileRes[] = [];
    for (let i = 0; i < 40; i += 1) {
      repliesBefore.push(await postJson<ProfileRes>(providerA.httpUrl, '/profile/request', { value: `${markerBefore}-${i}` }));
    }
    const apiABeforeValues = repliesBefore.filter((reply) => reply.providerRid === 'api-a').map((reply) => reply.value);
    const apiBBeforeValues = repliesBefore.filter((reply) => reply.providerRid === 'api-b').map((reply) => reply.value);
    ensure(apiABeforeValues.length > 0 && apiBBeforeValues.length > 0, 'RM-B2 expected both providers before scale-in.');
    const scaleOutA = await postJson<string[]>(providerA.httpUrl, '/evidence/wait', { contains: apiABeforeValues[apiABeforeValues.length - 1] });
    const scaleOutB = await postJson<string[]>(providerB.httpUrl, '/evidence/wait', { contains: apiBBeforeValues[apiBBeforeValues.length - 1] });
    ensure(
      countNewEvidence(scaleOutA, beforeA, 'profile-request|rid=api-a', markerBefore)
      + countNewEvidence(scaleOutB, beforeB, 'profile-request|rid=api-b', markerBefore) === 40,
      'RM-B2 expected both providers before scale-in.'
    );

    await cluster.stop(providerB);
    await new Promise((resolve) => setTimeout(resolve, 1000));
    const beforeAfterA = await getJson<string[]>(providerA.httpUrl, '/evidence');
    const beforeAfterB = await readEvidenceIgnoringStopped(providerB.httpUrl);
    const markerAfter = uniqueMarker('rm-b2-after');
    const repliesAfter: ProfileRes[] = [];
    for (let i = 0; i < 20; i += 1) {
      repliesAfter.push(await postJson<ProfileRes>(providerA.httpUrl, '/profile/request', { value: `${markerAfter}-${i}` }));
    }
    ensure(repliesAfter.every((reply) => reply.providerRid === 'api-a'), 'RM-B2 after scale-in should reach api-a only.');
    const afterA = await postJson<string[]>(providerA.httpUrl, '/evidence/wait', { contains: `${markerAfter}-19` });
    const afterB = await readEvidenceIgnoringStopped(providerB.httpUrl);
    const a = countNewEvidence(afterA, beforeAfterA, 'profile-request|rid=api-a', markerAfter);
    const b = countNewEvidence(afterB, beforeAfterB, 'profile-request|rid=api-b', markerAfter);
    ensure(a === 20 && b === 0, 'RM-B2 expected only api-a after scale-in.');
    console.log('scenario RM-B2 passed');
  } finally {
    await cluster.close();
  }
}

async function readEvidenceIgnoringStopped(url: string): Promise<string[]> {
  try {
    return await getJson<string[]>(url, '/evidence');
  } catch {
    return [];
  }
}
