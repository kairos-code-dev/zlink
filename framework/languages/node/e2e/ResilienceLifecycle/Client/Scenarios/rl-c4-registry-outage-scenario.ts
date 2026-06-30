import type { ProfileRes } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson } from '../Support/http-client';
import { startProvider } from '../Support/managed-provider';
import { startRegistry } from '../Support/managed-registry';
import {
  profileReq,
  waitTopologyReady,
  waitUntilAvailable,
  waitUntilDown
} from '../Support/resilience-helpers';
import { ensure } from '../Support/scenario-assert';
import type { ScenarioState } from '../Support/scenario-state';

export async function runRlC4(options: ClientOptions, state: ScenarioState): Promise<void> {
  const before = await postJson<ProfileRes>(
    options.consumerUrl,
    '/profile/request',
    profileReq('rl-c4-before-outage')
  );
  ensure(before.value === 'profile:fast', 'RL-C4 request failed before registry outage.');

  await postJson(options.registryUrl, '/shutdown');
  if (state.registryProcess !== undefined) {
    await state.registryProcess.stop();
    state.registryProcess = undefined;
  }
  await waitUntilDown(options.registryUrl);

  const during = await postJson<ProfileRes>(
    options.consumerUrl,
    '/profile/request',
    profileReq('rl-c4-during-outage')
  );
  ensure(during.value === 'profile:fast', 'RL-C4 existing channel failed during registry outage.');

  await waitForEitherEvidence(options, 'marker=rl-c4-before-outage', 'RL-C4 before-outage evidence missing.');
  await waitForEitherEvidence(options, 'marker=rl-c4-during-outage', 'RL-C4 during-outage evidence missing.');

  const restartedRegistry = startRegistry({
    registryMain: options.registryMain,
    logDir: options.logDir,
    name: 'registry-c4-restored',
    rid: 'registry',
    httpUrl: options.registryUrl,
    registryPubEndpoint: options.registryPubEndpoint,
    registryRouterEndpoint: options.registryRouterEndpoint
  });
  await restartedRegistry.waitReady();
  state.registryProcess = restartedRegistry;

  await postJson(options.providerAUrl, '/shutdown');
  if (state.providerAProcess !== undefined) {
    await state.providerAProcess.waitExited();
    state.providerAProcess = undefined;
  }
  await waitUntilDown(options.providerAUrl);

  const restartedProviderA = startProvider({
    providerMain: options.providerMain,
    logDir: options.logDir,
    registryRouterEndpoint: options.registryRouterEndpoint,
    name: 'api-a-c4-restored',
    rid: 'api-a',
    httpUrl: options.providerAUrl,
    channelEndpoint: options.providerAChannelEndpoint,
    evidenceFileName: 'api-a-c4-restored.evidence.log'
  });
  await restartedProviderA.waitReady();
  state.providerAProcess = restartedProviderA;
  await waitUntilAvailable(options.providerAUrl);
  await waitTopologyReady(options.registryUrl, 'api-a');

  const after = await postJson<ProfileRes>(
    options.consumerUrl,
    '/profile/request/new-client',
    profileReq('rl-c4-after-restart')
  );
  ensure(after.value === 'profile:fast', 'RL-C4 follow-up request failed after registry restart.');

  await waitForEitherEvidence(options, 'marker=rl-c4-after-restart', 'RL-C4 after-restart evidence missing.');

  console.log('scenario RL-C4 passed');
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
