import type { ClientOptions } from '../Support/client-options';
import { ensure } from '../Support/scenario-assert';
import {
  requestProfile,
  startProviderC,
  startRegistry,
  stopServer,
  waitForEvidence,
  waitForReadyTopologyProviderSet,
  waitForReplyEvidence,
  waitHttpDown
} from '../Support/discovery-scenario-support';
import { ManagedProcess } from '../Support/managed-process';

export async function runDrC3(options: ClientOptions): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-C3 requires registry-2-url.');
  ensure(options.registry3Url !== undefined, 'DR-C3 requires registry-3-url.');
  ensure(options.consumer2Url !== undefined, 'DR-C3 requires consumer-2-url.');
  ensure(options.providerBUrl !== undefined, 'DR-C3 requires provider-b-url.');
  ensure(options.providerCUrl !== undefined, 'DR-C3 requires provider-c-url.');
  ensure(options.providerCEndpoint !== undefined, 'DR-C3 requires provider-c-endpoint.');
  ensure(options.registryPubEndpoint !== undefined, 'DR-C3 requires registry-pub-endpoint.');
  ensure(options.registry2PubEndpoint !== undefined, 'DR-C3 requires registry-2-pub-endpoint.');
  ensure(options.registry3PubEndpoint !== undefined, 'DR-C3 requires registry-3-pub-endpoint.');
  ensure(options.registryRouterEndpoint !== undefined, 'DR-C3 requires registry-router-endpoint.');
  ensure(options.registry2RouterEndpoint !== undefined, 'DR-C3 requires registry-2-router-endpoint.');
  ensure(options.registry3RouterEndpoint !== undefined, 'DR-C3 requires registry-3-router-endpoint.');
  ensure(options.registryMain !== undefined, 'DR-C3 requires registry-main.');
  ensure(options.providerMain !== undefined, 'DR-C3 requires provider-main.');
  ensure(options.consumerMain !== undefined, 'DR-C3 requires consumer-main.');
  ensure(options.logDir !== undefined, 'DR-C3 requires log-dir.');

  const before = await requestProfile(options.consumerUrl, 'dr-c3-before', 'dr-c3');
  ensure(before.providerRid === 'api-a' || before.providerRid === 'api-b', 'DR-C3 pre-outage provider rid mismatch.');
  await waitForReplyEvidence(before, options.providerAUrl, options.providerBUrl);

  await stopServer(options.registryUrl);
  await stopServer(options.registry2Url);
  await stopServer(options.registry3Url);
  await waitHttpDown(options.registryUrl);
  await waitHttpDown(options.registry2Url);
  await waitHttpDown(options.registry3Url);

  const during = await requestProfile(options.consumerUrl, 'dr-c3-during', 'dr-c3');
  ensure(during.providerRid === 'api-a' || during.providerRid === 'api-b', 'DR-C3 during-outage provider rid mismatch.');
  await waitForReplyEvidence(during, options.providerAUrl, options.providerBUrl);

  await stopServer(options.providerAUrl);
  await stopServer(options.providerBUrl);
  await stopServer(options.consumer2Url);

  const started: ManagedProcess[] = [];
  try {
    started.push(await startRegistry({
      name: 'reg-1-after-all-outage',
      rid: 'reg-1',
      registryId: '1',
      httpUrl: options.registryUrl,
      pubEndpoint: options.registryPubEndpoint,
      routerEndpoint: options.registryRouterEndpoint,
      peers: [options.registry2PubEndpoint, options.registry3PubEndpoint],
      main: options.registryMain,
      logDir: options.logDir
    }));
    started.push(await startRegistry({
      name: 'reg-2-after-all-outage',
      rid: 'reg-2',
      registryId: '2',
      httpUrl: options.registry2Url,
      pubEndpoint: options.registry2PubEndpoint,
      routerEndpoint: options.registry2RouterEndpoint,
      peers: [options.registryPubEndpoint, options.registry3PubEndpoint],
      main: options.registryMain,
      logDir: options.logDir
    }));
    started.push(await startRegistry({
      name: 'reg-3-after-all-outage',
      rid: 'reg-3',
      registryId: '3',
      httpUrl: options.registry3Url,
      pubEndpoint: options.registry3PubEndpoint,
      routerEndpoint: options.registry3RouterEndpoint,
      peers: [options.registryPubEndpoint, options.registry2PubEndpoint],
      main: options.registryMain,
      logDir: options.logDir
    }));
    started.push(await startProviderC(options));
    await waitForReadyTopologyProviderSet(options.registryUrl, ['api-c']);
    await waitForReadyTopologyProviderSet(options.registry2Url, ['api-c']);
    await waitForReadyTopologyProviderSet(options.registry3Url, ['api-c']);

    const recoveredConsumer = ManagedProcess.start({
      name: 'consumer-reg2-recovered',
      rid: 'consumer-reg2-recovered',
      main: options.consumerMain,
      logDir: options.logDir,
      args: [
        '--http-url', options.consumer2Url,
        '--registry-router-endpoint', options.registry2RouterEndpoint,
        '--trace-label', 'consumer-reg2-recovered',
        '--log-dir', options.logDir
      ]
    });
    started.push(recoveredConsumer);
    await recoveredConsumer.waitReady();

    const after = await requestProfile(options.consumer2Url, 'dr-c3-after', 'dr-c3');
    ensure(after.providerRid === 'api-c', 'DR-C3 recovered registry did not route to api-c.');
    const evidence = await waitForEvidence(options.providerCUrl, after.marker ?? '');
    ensure(
      evidence.some((entry) => entry.includes(after.marker ?? '') && entry.includes('rid=api-c')),
      `DR-C3 evidence was not recorded for ${after.marker}.`
    );
  } finally {
    for (const process of started.reverse()) {
      await process.stop();
    }
  }

  console.log('scenario DR-C3 passed');
}
