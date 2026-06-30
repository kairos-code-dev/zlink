import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { startProvider } from '../Support/managed-provider';
import {
  profileReq,
  sendRequestBatch,
  waitTopologyReady,
  waitUntilDown
} from '../Support/resilience-helpers';
import { ensure } from '../Support/scenario-assert';
import type { ScenarioState } from '../Support/scenario-state';
import type { ProfileRes } from '../../Shared/messages';

export async function runRlA1(options: ClientOptions, state: ScenarioState): Promise<void> {
  await postJson(options.providerBUrl, '/shutdown');
  await waitUntilDown(options.providerBUrl);
  state.providerBProcess = undefined;

  for (let i = 0; i < 12; i += 1) {
    const marker = `rl-a1-down-${i}`;
    const reply = await postJson<ProfileRes>(options.consumerUrl, '/profile/request', profileReq(marker));
    ensure(reply.providerRid === 'api-a', 'RL-A1 request during api-b restart did not use surviving provider.');
  }
  await postJson<string[]>(options.providerAUrl, '/evidence/wait', { contains: 'marker=rl-a1-down-' });

  const restarted = startProvider({
    providerMain: options.providerMain,
    logDir: options.logDir,
    registryRouterEndpoint: options.registryRouterEndpoint,
    name: 'api-b-restart',
    rid: 'api-b',
    httpUrl: options.providerBUrl,
    channelEndpoint: options.providerBChannelEndpoint,
    evidenceFileName: 'api-b-restart.evidence.log'
  });
  await restarted.waitReady();
  state.providerBProcess = restarted;
  await waitTopologyReady(options.registryUrl, 'api-b');

  const sawApiB = await sendRequestBatch(options.consumerUrl, 'rl-a1-restored', 'api-b');
  ensure(sawApiB, 'RL-A1 restored traffic did not reach restarted api-b.');
  await postJson<string[]>(options.providerBUrl, '/evidence/wait', { contains: 'marker=rl-a1-restored-' });

  console.log('scenario RL-A1 passed');
}
