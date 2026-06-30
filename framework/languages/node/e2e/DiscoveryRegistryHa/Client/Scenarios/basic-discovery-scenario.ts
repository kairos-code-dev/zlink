import {
  ZLinkServiceRole,
  ZLinkTopologyState
} from '@zlink-systems/framework';
import type { ProfileReply } from '../../Shared/messages';
import { getJson, getJsonWithin, postJson } from '../Support/http-client';
import { ManagedProcess } from '../Support/managed-process';
import { ensure } from '../Support/scenario-assert';

export async function runDrA1(options: {
  readonly registryUrl: string;
  readonly consumerUrl: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
}): Promise<void> {
  ensure(options.providerBUrl !== undefined, 'DR-A1 requires provider-b-url.');
  const reply = await postJson<ProfileReply>(options.consumerUrl, '/profile/request', { value: 'dr-a1' });
  ensure(reply.value === 'profile:dr-a1', 'DR-A1 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'DR-A1 provider rid mismatch.');

  const topology = await getJson<Array<{ channelName: string; serviceRole: number; state: number }>>(
    options.registryUrl,
    '/registry/topology'
  );
  const readyProviders = topology.filter((entry) =>
    entry.channelName === 'profile'
    && entry.serviceRole === ZLinkServiceRole.Router
    && entry.state === ZLinkTopologyState.Ready
  ).length;
  ensure(readyProviders >= 2, 'DR-A1 expected two ready providers in registry topology.');

  const providerEvidence = [
    ...await getJson<string[]>(options.providerAUrl, '/evidence'),
    ...await getJson<string[]>(options.providerBUrl, '/evidence')
  ];
  ensure(providerEvidence.some((entry) => entry.includes('value=dr-a1')), 'DR-A1 provider evidence missing.');
  console.log('scenario DR-A1 passed');
}

export async function runDrA2(options: {
  readonly registry2Url?: string;
  readonly consumerUrl: string;
  readonly providerAUrl: string;
}): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-A2 requires registry-2-url.');

  const members = await waitForMemberPeer(options.registry2Url, 'api-a');
  ensure(members.some((entry) => String(entry.routingId) === 'api-a'), 'DR-A2 reg-2 member peer did not include api-a.');

  const reply = await postJson<ProfileReply>(options.consumerUrl, '/profile/request', { value: 'dr-a2' });
  ensure(reply.value === 'profile:dr-a2', 'DR-A2 reply value mismatch.');
  ensure(reply.providerRid === 'api-a', 'DR-A2 expected reg-1 provider api-a through reg-2 discovery.');

  const providerEvidence = await getJson<string[]>(options.providerAUrl, '/evidence');
  ensure(providerEvidence.some((entry) => entry.includes('value=dr-a2')), 'DR-A2 provider evidence missing.');
  console.log('scenario DR-A2 passed');
}

export async function runDrA3(options: {
  readonly registryUrl: string;
  readonly registry2Url?: string;
  readonly registry3Url?: string;
  readonly consumerUrl: string;
  readonly consumer2Url?: string;
  readonly consumer3Url?: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
}): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-A3 requires registry-2-url.');
  ensure(options.registry3Url !== undefined, 'DR-A3 requires registry-3-url.');
  ensure(options.consumer2Url !== undefined, 'DR-A3 requires consumer-2-url.');
  ensure(options.consumer3Url !== undefined, 'DR-A3 requires consumer-3-url.');
  ensure(options.providerBUrl !== undefined, 'DR-A3 requires provider-b-url.');

  for (const registryUrl of [options.registryUrl, options.registry2Url, options.registry3Url]) {
    await waitForConnectedPeerRegistryCount(registryUrl, 2);
    await waitForMemberPeers(registryUrl, ['api-a', 'api-b']);
  }

  const checks = [
    { url: options.consumerUrl, value: 'dr-a3-reg-1' },
    { url: options.consumer2Url, value: 'dr-a3-reg-2' },
    { url: options.consumer3Url, value: 'dr-a3-reg-3' }
  ];
  for (const check of checks) {
    const reply = await postJson<ProfileReply>(check.url, '/profile/request', { value: check.value });
    ensure(reply.value === `profile:${check.value}`, `DR-A3 reply value mismatch for ${check.value}.`);
    ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', `DR-A3 provider rid mismatch for ${check.value}.`);
  }

  const providerEvidence = [
    ...await getJson<string[]>(options.providerAUrl, '/evidence'),
    ...await getJson<string[]>(options.providerBUrl, '/evidence')
  ];
  for (const check of checks) {
    ensure(
      providerEvidence.some((entry) => entry.includes(`value=${check.value}`)),
      `DR-A3 provider evidence missing for ${check.value}.`
    );
  }
  console.log('scenario DR-A3 passed');
}

export async function runDrA4(options: {
  readonly registry2Url?: string;
  readonly consumerUrl: string;
  readonly providerAUrl: string;
  readonly duplicateProviderUrl?: string;
}): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-A4 requires registry-2-url.');
  ensure(options.duplicateProviderUrl !== undefined, 'DR-A4 requires duplicate-provider-url.');

  const marker = `dr-a4-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileReply>(options.consumerUrl, '/profile/request', { value: 'dr-a4', marker });
  ensure(reply.value === 'profile:dr-a4', 'DR-A4 reply value mismatch.');
  ensure(reply.providerRid === 'api-a', 'DR-A4 same-rid providers should report api-a.');
  ensure(reply.marker === marker, 'DR-A4 marker mismatch.');

  const evidence = await waitForEitherProviderEvidence(options.providerAUrl, options.duplicateProviderUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes('rid=api-a')),
    'DR-A4 provider evidence was not recorded.'
  );
  console.log('scenario DR-A4 passed');
}

export async function runDrB1(options: {
  readonly registry2Url?: string;
  readonly registry3Url?: string;
  readonly consumer2Url?: string;
  readonly consumer3Url?: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
}): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-B1 requires registry-2-url.');
  ensure(options.registry3Url !== undefined, 'DR-B1 requires registry-3-url.');
  ensure(options.consumer2Url !== undefined, 'DR-B1 requires consumer-2-url.');
  ensure(options.consumer3Url !== undefined, 'DR-B1 requires consumer-3-url.');
  ensure(options.providerBUrl !== undefined, 'DR-B1 requires provider-b-url.');

  const cases = [
    { name: 'reg-2', registryUrl: options.registry2Url, consumerUrl: options.consumer2Url },
    { name: 'reg-3', registryUrl: options.registry3Url, consumerUrl: options.consumer3Url }
  ];
  for (const registryCase of cases) {
    await waitForConnectedPeerRegistryCount(registryCase.registryUrl, 1);
    await waitForMemberPeers(registryCase.registryUrl, ['api-a', 'api-b']);

    const marker = `dr-b1-${registryCase.name}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    const reply = await postJson<ProfileReply>(registryCase.consumerUrl, '/profile/request', { value: 'dr-b1', marker });
    ensure(reply.value === 'profile:dr-b1', `DR-B1 ${registryCase.name} reply value mismatch.`);
    ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', `DR-B1 ${registryCase.name} provider rid mismatch.`);
    ensure(reply.marker === marker, `DR-B1 ${registryCase.name} marker mismatch.`);

    const evidenceUrl = reply.providerRid === 'api-a' ? options.providerAUrl : options.providerBUrl;
    const evidence = await waitForEvidence(evidenceUrl, marker);
    ensure(
      evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
      `DR-B1 ${registryCase.name} provider evidence was not recorded.`
    );
  }
  console.log('scenario DR-B1 passed');
}

export async function runDrB2(options: {
  readonly registryUrl: string;
  readonly consumerUrl: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
}): Promise<void> {
  ensure(options.providerBUrl !== undefined, 'DR-B2 requires provider-b-url.');

  await waitForMemberPeer(options.registryUrl, 'api-a');

  const marker = `dr-b2-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileReply>(options.consumerUrl, '/profile/request', { value: 'dr-b2', marker });
  ensure(reply.value === 'profile:dr-b2', 'DR-B2 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'DR-B2 provider rid mismatch.');
  ensure(reply.marker === marker, 'DR-B2 marker mismatch.');

  const evidenceUrl = reply.providerRid === 'api-a' ? options.providerAUrl : options.providerBUrl;
  const evidence = await waitForEvidence(evidenceUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
    'DR-B2 provider evidence was not recorded.'
  );
  console.log('scenario DR-B2 passed');
}

export async function runDrB3(options: {
  readonly registryUrl: string;
  readonly registry2Url?: string;
  readonly consumerUrl: string;
  readonly consumer2Url?: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
}): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-B3 requires registry-2-url.');
  ensure(options.consumer2Url !== undefined, 'DR-B3 requires consumer-2-url.');
  ensure(options.providerBUrl !== undefined, 'DR-B3 requires provider-b-url.');

  const cases = [
    { name: 'reg-2', registryUrl: options.registry2Url, consumerUrl: options.consumer2Url },
    { name: 'survivor', registryUrl: options.registryUrl, consumerUrl: options.consumerUrl }
  ];
  for (const registryCase of cases) {
    await waitForConnectedPeerRegistryCount(registryCase.registryUrl, 1);
    await waitForMemberPeers(registryCase.registryUrl, ['api-a', 'api-b']);

    const marker = `dr-b3-${registryCase.name}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    const reply = await postJson<ProfileReply>(registryCase.consumerUrl, '/profile/request', { value: 'dr-b3', marker });
    ensure(reply.value === 'profile:dr-b3', `DR-B3 ${registryCase.name} reply value mismatch.`);
    ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', `DR-B3 ${registryCase.name} provider rid mismatch.`);
    ensure(reply.marker === marker, `DR-B3 ${registryCase.name} marker mismatch.`);

    const evidenceUrl = reply.providerRid === 'api-a' ? options.providerAUrl : options.providerBUrl;
    const evidence = await waitForEvidence(evidenceUrl, marker);
    ensure(
      evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
      `DR-B3 ${registryCase.name} provider evidence was not recorded.`
    );
  }
  console.log('scenario DR-B3 passed');
}

export async function runDrC1(options: {
  readonly registryUrl: string;
  readonly registry2Url?: string;
  readonly consumerUrl: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
}): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-C1 requires registry-2-url.');
  ensure(options.providerBUrl !== undefined, 'DR-C1 requires provider-b-url.');

  await waitForMemberPeer(options.registryUrl, 'api-a');

  const marker = `dr-c1-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileReply>(options.consumerUrl, '/profile/request', { value: 'dr-c1', marker });
  ensure(reply.value === 'profile:dr-c1', 'DR-C1 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'DR-C1 provider rid mismatch.');
  ensure(reply.marker === marker, 'DR-C1 marker mismatch.');

  const evidenceUrl = reply.providerRid === 'api-a' ? options.providerAUrl : options.providerBUrl;
  const evidence = await waitForEvidence(evidenceUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
    'DR-C1 provider evidence was not recorded.'
  );

  await assertDeadRegistryFails(options.registry2Url);
  console.log('scenario DR-C1 passed');
}

export async function runDrC2(options: {
  readonly registry2Url?: string;
  readonly consumer2Url?: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
}): Promise<void> {
  ensure(options.registry2Url !== undefined, 'DR-C2 requires registry-2-url.');
  ensure(options.consumer2Url !== undefined, 'DR-C2 requires consumer-2-url.');
  ensure(options.providerBUrl !== undefined, 'DR-C2 requires provider-b-url.');

  await waitForConnectedPeerRegistryCount(options.registry2Url, 1);
  await waitForMemberPeers(options.registry2Url, ['api-a', 'api-b']);

  const marker = `dr-c2-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileReply>(options.consumer2Url, '/profile/request', { value: 'dr-c2', marker });
  ensure(reply.value === 'profile:dr-c2', 'DR-C2 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'DR-C2 provider rid mismatch.');
  ensure(reply.marker === marker, 'DR-C2 marker mismatch.');

  const evidenceUrl = reply.providerRid === 'api-a' ? options.providerAUrl : options.providerBUrl;
  const evidence = await waitForEvidence(evidenceUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
    'DR-C2 provider evidence was not recorded.'
  );
  console.log('scenario DR-C2 passed');
}

export async function runDrC3(options: {
  readonly registryUrl: string;
  readonly registry2Url?: string;
  readonly registry3Url?: string;
  readonly consumerUrl: string;
  readonly consumer2Url?: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
  readonly providerCUrl?: string;
  readonly providerCEndpoint?: string;
  readonly registryPubEndpoint?: string;
  readonly registry2PubEndpoint?: string;
  readonly registry3PubEndpoint?: string;
  readonly registryRouterEndpoint?: string;
  readonly registry2RouterEndpoint?: string;
  readonly registry3RouterEndpoint?: string;
  readonly registryMain?: string;
  readonly providerMain?: string;
  readonly consumerMain?: string;
  readonly logDir?: string;
}): Promise<void> {
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

export async function runDrD2(options: {
  readonly registryUrl: string;
  readonly consumerUrl: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
}): Promise<void> {
  ensure(options.providerBUrl !== undefined, 'DR-D2 requires provider-b-url.');

  await waitForMemberPeer(options.registryUrl, 'api-a');

  const marker = `dr-d2-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileReply>(options.consumerUrl, '/profile/request', { value: 'dr-d2', marker });
  ensure(reply.value === 'profile:dr-d2', 'DR-D2 reply value mismatch.');
  ensure(reply.providerRid === 'api-a' || reply.providerRid === 'api-b', 'DR-D2 provider rid mismatch.');
  ensure(reply.marker === marker, 'DR-D2 marker mismatch.');

  const evidenceUrl = reply.providerRid === 'api-a' ? options.providerAUrl : options.providerBUrl;
  const evidence = await waitForEvidence(evidenceUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
    'DR-D2 provider evidence was not recorded.'
  );
  console.log('scenario DR-D2 passed');
}

export async function runDrD1(options: {
  readonly embeddedUrl?: string;
  readonly embeddedConsumerUrl?: string;
}): Promise<void> {
  ensure(options.embeddedUrl !== undefined, 'DR-D1 requires embedded-url.');
  ensure(options.embeddedConsumerUrl !== undefined, 'DR-D1 requires embedded-consumer-url.');

  const marker = `dr-d1-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileReply>(options.embeddedConsumerUrl, '/profile/request', { value: 'dr-d1', marker });
  ensure(reply.value === 'profile:dr-d1', 'DR-D1 reply value mismatch.');
  ensure(reply.providerRid === 'embedded-api', 'DR-D1 should route to embedded-api.');
  ensure(reply.marker === marker, 'DR-D1 marker mismatch.');

  const evidence = await waitForEvidence(options.embeddedUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes('rid=embedded-api')),
    'DR-D1 embedded provider evidence was not recorded.'
  );
  console.log('scenario DR-D1 passed');
}

export async function runDrD3(options: {
  readonly embeddedUrl?: string;
  readonly embeddedConsumerUrl?: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
}): Promise<void> {
  ensure(options.embeddedUrl !== undefined, 'DR-D3 requires embedded-url.');
  ensure(options.embeddedConsumerUrl !== undefined, 'DR-D3 requires embedded-consumer-url.');
  ensure(options.providerBUrl !== undefined, 'DR-D3 requires provider-b-url.');

  await waitForMemberPeer(options.embeddedUrl, 'api-a');

  const marker = `dr-d3-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileReply>(options.embeddedConsumerUrl, '/profile/request', { value: 'dr-d3', marker });
  ensure(reply.value === 'profile:dr-d3', 'DR-D3 reply value mismatch.');
  ensure(
    reply.providerRid === 'api-a' || reply.providerRid === 'api-b' || reply.providerRid === 'embedded-api-mixed',
    'DR-D3 provider rid mismatch.'
  );
  ensure(reply.marker === marker, 'DR-D3 marker mismatch.');

  const evidenceUrl = reply.providerRid === 'api-a'
    ? options.providerAUrl
    : reply.providerRid === 'api-b'
      ? options.providerBUrl
      : options.embeddedUrl;
  const evidence = await waitForEvidence(evidenceUrl, marker);
  ensure(
    evidence.some((entry) => entry.includes(marker) && entry.includes(`rid=${reply.providerRid}`)),
    'DR-D3 provider evidence was not recorded.'
  );
  console.log('scenario DR-D3 passed');
}

export async function runDrD4(options: {
  readonly registryUrl: string;
  readonly probeUrl?: string;
}): Promise<void> {
  ensure(options.probeUrl !== undefined, 'DR-D4 requires probe-url.');

  await waitForReadyTopologyProviderSet(options.registryUrl, ['api-a']);
  await waitForReadyTopologyProviderSet(options.probeUrl, ['api-a']);

  const local = normalizeTopology(await getJson<readonly TopologySnapshotEntry[]>(options.registryUrl, '/registry/topology'));
  const remote = normalizeTopology(await getJson<readonly TopologySnapshotEntry[]>(options.probeUrl, '/registry/topology'));
  ensure(local.length > 0, 'DR-D4 topology snapshot was empty.');
  ensure(
    local.length === remote.length && local.every((entry, index) => entry === remote[index]),
    'DR-D4 in-process and remote topology snapshots did not match.'
  );
  console.log('scenario DR-D4 passed');
}

interface TopologySnapshotEntry {
  readonly channelName?: string;
  readonly routingId?: string;
  readonly endpoint?: string;
  readonly state?: string | number;
  readonly serviceRole?: string | number;
}

async function waitForMemberPeer(
  registryUrl: string,
  expectedRid: string
): Promise<Array<{ routingId?: string; endpoint: string }>> {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    const members = await getJson<Array<{ routingId?: string; endpoint: string }>>(registryUrl, '/registry/member-peers');
    if (members.some((entry) => String(entry.routingId) === expectedRid)) {
      return members;
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  throw new Error(`Timed out waiting for member peer '${expectedRid}'.`);
}

async function waitForEitherProviderEvidence(
  providerAUrl: string,
  duplicateProviderUrl: string,
  marker: string
): Promise<readonly string[]> {
  const waitA = waitForEvidence(providerAUrl, marker);
  const waitB = waitForEvidence(duplicateProviderUrl, marker);
  return await Promise.any([waitA, waitB]);
}

async function waitForEvidence(providerUrl: string, marker: string): Promise<readonly string[]> {
  return await postJson<string[]>(providerUrl, '/evidence/wait', { contains: marker, timeoutMilliseconds: 10000 });
}

async function waitForMemberPeers(
  registryUrl: string,
  expectedRids: readonly string[]
): Promise<Array<{ routingId?: string; endpoint: string }>> {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    const members = await getJson<Array<{ routingId?: string; endpoint: string }>>(registryUrl, '/registry/member-peers');
    if (expectedRids.every((rid) => members.some((entry) => String(entry.routingId) === rid))) {
      return members;
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  throw new Error(`Timed out waiting for member peers '${expectedRids.join(', ')}'.`);
}

async function waitForConnectedPeerRegistryCount(registryUrl: string, minimumCount: number): Promise<void> {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    const status = await getJson<{ connectedPeerRegistryCount?: number }>(registryUrl, '/registry/status');
    if ((status.connectedPeerRegistryCount ?? 0) >= minimumCount) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  throw new Error(`Timed out waiting for connected peer registry count >= ${minimumCount}.`);
}

async function assertDeadRegistryFails(registryUrl: string): Promise<void> {
  try {
    await getJsonWithin<unknown>(registryUrl, '/registry/status', 500);
  } catch {
    return;
  }
  throw new Error('DR-C1 dead registry endpoint did not fail within the bounded timeout.');
}

async function requestProfile(consumerUrl: string, phase: string, value: string): Promise<ProfileReply> {
  const marker = `${phase}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileReply>(consumerUrl, '/profile/request', { value, marker });
  ensure(reply.value === `profile:${value}`, `DR-C3 ${phase} reply value mismatch.`);
  ensure(reply.marker === marker, `DR-C3 ${phase} marker mismatch.`);
  return reply;
}

async function waitForReplyEvidence(reply: ProfileReply, providerAUrl: string, providerBUrl: string): Promise<void> {
  const providerUrl = reply.providerRid === 'api-a' ? providerAUrl : providerBUrl;
  const evidence = await waitForEvidence(providerUrl, reply.marker ?? '');
  ensure(
    evidence.some((entry) => entry.includes(reply.marker ?? '') && entry.includes(`rid=${reply.providerRid}`)),
    `DR-C3 evidence was not recorded for ${reply.marker}.`
  );
}

async function stopServer(baseUrl: string): Promise<void> {
  try {
    await postJson<unknown>(baseUrl, '/shutdown', {});
  } catch {
  }
}

async function waitHttpDown(baseUrl: string): Promise<void> {
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    try {
      await getJsonWithin<unknown>(baseUrl, '/health', 200);
    } catch {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`Timed out waiting for ${baseUrl} to stop.`);
}

async function startRegistry(options: {
  readonly name: string;
  readonly rid: string;
  readonly registryId: string;
  readonly httpUrl: string;
  readonly pubEndpoint: string;
  readonly routerEndpoint: string;
  readonly peers: readonly string[];
  readonly main: string;
  readonly logDir: string;
}): Promise<ManagedProcess> {
  const args = [
    '--rid', options.rid,
    '--registry-id', options.registryId,
    '--http-url', options.httpUrl,
    '--registry-pub-endpoint', options.pubEndpoint,
    '--registry-router-endpoint', options.routerEndpoint,
    '--log-dir', options.logDir
  ];
  for (const peer of options.peers) {
    args.push('--peer', peer);
  }
  const process = ManagedProcess.start({
    name: options.name,
    rid: options.rid,
    main: options.main,
    args,
    logDir: options.logDir
  });
  await process.waitReady();
  return process;
}

async function startProviderC(options: {
  readonly providerMain?: string;
  readonly providerCUrl?: string;
  readonly providerCEndpoint?: string;
  readonly registryRouterEndpoint?: string;
  readonly registry2RouterEndpoint?: string;
  readonly registry3RouterEndpoint?: string;
  readonly logDir?: string;
}): Promise<ManagedProcess> {
  const process = ManagedProcess.start({
    name: 'api-c-after-all-outage',
    rid: 'api-c',
    main: options.providerMain ?? '',
    logDir: options.logDir ?? '',
    args: [
      '--rid', 'api-c',
      '--http-url', options.providerCUrl ?? '',
      '--registry-router-endpoint', options.registryRouterEndpoint ?? '',
      '--registry-router-endpoint', options.registry2RouterEndpoint ?? '',
      '--registry-router-endpoint', options.registry3RouterEndpoint ?? '',
      '--channel-endpoint', options.providerCEndpoint ?? '',
      '--evidence-file', `${options.logDir}/api-c-after-all-outage.evidence.log`,
      '--log-dir', options.logDir ?? ''
    ]
  });
  await process.waitReady();
  return process;
}

async function waitForReadyTopologyProviderSet(registryUrl: string, expectedRids: readonly string[]): Promise<void> {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    const topology = await getJson<Array<{ routingId?: string }>>(registryUrl, '/registry/topology');
    const actual = [...new Set(topology.map((entry) => String(entry.routingId)).filter((rid) => rid.length > 0))].sort();
    const expected = [...expectedRids].sort();
    if (actual.length === expected.length && actual.every((rid, index) => rid === expected[index])) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  throw new Error(`Timed out waiting for ready topology providers '${expectedRids.join(', ')}'.`);
}

function normalizeTopology(entries: readonly TopologySnapshotEntry[]): string[] {
  return entries
    .map((entry) => [
      entry.channelName ?? '',
      entry.routingId ?? '',
      entry.endpoint ?? '',
      entry.state ?? '',
      entry.serviceRole ?? ''
    ].map(String).join('|'))
    .sort();
}
