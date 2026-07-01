import type { ProfileRes } from '../../Shared/messages';
import { getJson, getJsonWithin, postJson } from './http-client';
import { ManagedProcess } from './managed-process';
import { ensure } from './scenario-assert';

export interface TopologySnapshotEntry {
  readonly channelName?: string;
  readonly routingId?: string;
  readonly endpoint?: string;
  readonly state?: string | number;
  readonly serviceRole?: string | number;
}

export async function waitForMemberPeer(
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

export async function waitForEitherProviderEvidence(
  providerAUrl: string,
  duplicateProviderUrl: string,
  marker: string
): Promise<readonly string[]> {
  const waitA = waitForEvidence(providerAUrl, marker);
  const waitB = waitForEvidence(duplicateProviderUrl, marker);
  return await Promise.any([waitA, waitB]);
}

export async function waitForEvidence(providerUrl: string, marker: string): Promise<readonly string[]> {
  return await postJson<string[]>(providerUrl, '/evidence/wait', { contains: marker, timeoutMilliseconds: 10000 });
}

export async function waitForMemberPeers(
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

export async function waitForConnectedPeerRegistryCount(registryUrl: string, minimumCount: number): Promise<void> {
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

export async function assertDeadRegistryFails(registryUrl: string): Promise<void> {
  try {
    await getJsonWithin<unknown>(registryUrl, '/registry/status', 500);
  } catch {
    return;
  }
  throw new Error('DR-C1 dead registry endpoint did not fail within the bounded timeout.');
}

export async function requestProfile(consumerUrl: string, phase: string, value: string): Promise<ProfileRes> {
  const marker = `${phase}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  const reply = await postJson<ProfileRes>(consumerUrl, '/profile/request', { value, marker });
  ensure(reply.value === `profile:${value}`, `DR-C3 ${phase} reply value mismatch.`);
  ensure(reply.marker === marker, `DR-C3 ${phase} marker mismatch.`);
  return reply;
}

export async function waitForReplyEvidence(reply: ProfileRes, providerAUrl: string, providerBUrl: string): Promise<void> {
  const providerUrl = reply.providerRid === 'api-a' ? providerAUrl : providerBUrl;
  const evidence = await waitForEvidence(providerUrl, reply.marker ?? '');
  ensure(
    evidence.some((entry) => entry.includes(reply.marker ?? '') && entry.includes(`rid=${reply.providerRid}`)),
    `DR-C3 evidence was not recorded for ${reply.marker}.`
  );
}

export async function stopServer(baseUrl: string): Promise<void> {
  try {
    await postJson<unknown>(baseUrl, '/shutdown', {});
  } catch {
  }
}

export async function waitHttpDown(baseUrl: string): Promise<void> {
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

export async function startRegistry(options: {
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

export async function startProviderC(options: {
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

export async function waitForReadyTopologyProviderSet(registryUrl: string, expectedRids: readonly string[]): Promise<void> {
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

export function normalizeTopology(entries: readonly TopologySnapshotEntry[]): string[] {
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
