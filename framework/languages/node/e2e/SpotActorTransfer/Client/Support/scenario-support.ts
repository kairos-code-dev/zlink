import {
  ZlinkStreamDispatchMode,
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  type ZlinkStreamConnector
} from '@zlink-systems/stream-connector';
import {
  SpotActorTransferNames,
  type ActorCreateReq,
  type ActorCreateRes,
  type ActorEvidence,
  type ActorRefSnapshotRes,
  type BindActorSessionReq,
  type BindActorSessionRes,
  type BoundPushNotify,
  type BoundPushReq,
  type BoundPushRes,
  type CreateSpotReq,
  type CreateSpotRes,
  type EvidenceWaitReq,
  type GateReleaseRes,
  type JoinTargetReq,
  type JoinTargetRes,
  type ProbeReq,
  type ProbeRes
} from '../../Shared/messages.js';
import {
  browserE2eArgs
} from '../../../browser-client-runtime';
import { ZLinkHttpClient, type ZLinkHttpClient as HttpClient } from '@zlink-systems/http-client';

export { SpotActorTransferNames };

export interface ClientOptions {
  nodeAUrl: string;
  nodeBUrl: string;
  sessionAStreamEndpoint: string;
  sessionBStreamEndpoint: string;
  scenario: string;
}

export const options = parseOptions(browserE2eArgs());
export const nodeA = ZLinkHttpClient.create(options.nodeAUrl).timeout(40000).build();
export const nodeB = ZLinkHttpClient.create(options.nodeBUrl).timeout(40000).build();

export async function runRemoteTransfer(scenario: string, actorId: string, actorType: string, stateVersion: number, stateful: boolean): Promise<void> {
  const spotRid = unique('spot-remote');
  await createSpot(nodeB, spotRid);
  await waitSpotRef(nodeA, spotRid, 'actor-b');
  const sourceActor = await createActor(nodeA, actorId, actorType, stateVersion);
  require(sourceActor.nodeRid === 'actor-a', `${scenario} source actor was created on '${sourceActor.nodeRid}'.`);
  require((await joinActor(nodeA, actorId, { scenario, targetSpotRid: spotRid })).accepted, `${scenario} join failed.`);
  const probe = await probeActor(nodeB, actorId, scenario, 'after-transfer');
  require(probe.nodeRid === 'actor-b' && (!stateful || probe.stateVersion === stateVersion), `${scenario} target state mismatch.`);
  const source = await waitEvidence(nodeA, [
    `transfer|${actorId}|transfer_out|${stateVersion}`,
    `transfer|${actorId}|leave|${stateVersion}`,
    `${scenario}|${actorId}|commit_request|after-source-leave`,
    `${scenario}|${actorId}|commit_ack|${spotRid}`,
    `${scenario}|${actorId}|success_reply|${spotRid}`
  ]);
  const target = await waitEvidence(nodeB, [
    `${scenario}|${actorId}|admission|spot=${spotRid}`,
    `transfer|${actorId}|transfer_in|${stateVersion}`,
    `transfer|${actorId}|joined|${spotRid}:${stateVersion}`,
    `${scenario}|${actorId}|location_committed|node=actor-b|spot=${spotRid}`
  ]);
  require(source.length > 0 && target.length > 0, `${scenario} transfer evidence missing.`);
  assertOrder(mergeEvidence(source, target), actorId, [
    'admission',
    'transfer_out',
    'leave',
    'commit_request',
    'transfer_in',
    'joined',
    'location_committed',
    'commit_ack',
    'success_reply'
  ]);
}

export async function assertSourceFailure(label: string, actorType: string, expectedKind: string): Promise<void> {
  const actorId = unique(`actor-fail-${label}`);
  const spotRid = unique(`spot-fail-${label}`);
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, actorType, 70);
  require(!(await joinActor(nodeA, actorId, { scenario: 'ST-C3', targetSpotRid: spotRid })).accepted, `ST-C3 ${label} failure returned success.`);
  const source = await getEvidence(nodeA);
  const target = await getEvidence(nodeB);
  require(has(source, actorId, expectedKind), `ST-C3 ${label} evidence missing.`);
  require(!has(target, actorId, 'joined'), `ST-C3 target joined after ${label} failure.`);
}

export async function connectAndBind(
  endpoint: string,
  scenario: string,
  actor: ActorCreateRes,
  transferId: string
): Promise<ZlinkStreamConnector> {
  const connector = zlinkStreamConnectorFactory.create({
    endpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    waitTimeoutMs: 15000,
    requestTimeoutMs: 10000
  });
  await connector.connect();
  const bound = await connector.request({
    scenario,
    actorId: actor.actorId,
    nodeRid: actor.nodeRid,
    generation: actor.generation,
    transferId
  } satisfies BindActorSessionReq).packetName(SpotActorTransferNames.packetBindActor).submit<BindActorSessionRes>();
  require(bound.actorId === actor.actorId, `${scenario} session bind mismatch.`);
  await delay(500);
  return connector;
}

export async function assertBoundPush(
  connector: ZlinkStreamConnector,
  _node: HttpClient,
  actorId: string,
  scenario: string,
  marker: string,
  expectedNode: string
): Promise<void> {
  const pushed = connector.waitFor<BoundPushNotify>(SpotActorTransferNames.packetBoundNotify)
    .where((message) => message.payload.scenario === scenario && message.payload.marker === marker)
    .timeout(15000).submit();
  const reply = await connector.request({ scenario, marker } satisfies BoundPushReq)
    .packetName(SpotActorTransferNames.packetBoundPush)
    .timeout(15000)
    .submit<BoundPushRes>();
  const notify = await pushed;
  require(reply.nodeRid === expectedNode && notify.payload.nodeRid === expectedNode, `${scenario} bound push node mismatch.`);
}

export async function assertHttpBoundPush(
  connector: ZlinkStreamConnector,
  node: HttpClient,
  actorId: string,
  scenario: string,
  marker: string,
  expectedNode: string
): Promise<void> {
  const pushed = connector.waitFor<BoundPushNotify>(SpotActorTransferNames.packetBoundNotify)
    .where((message) => message.payload.scenario === scenario && message.payload.marker === marker)
    .timeout(15000).submit();
  const reply = await post<BoundPushRes>(node, `/actors/${actorId}/bound-push`, { scenario, marker } satisfies BoundPushReq);
  const notify = await pushed;
  require(reply.nodeRid === expectedNode && notify.payload.nodeRid === expectedNode, `${scenario} bound push node mismatch.`);
}

export async function createSpot(node: HttpClient, spotRid: string, mode = 'accept'): Promise<CreateSpotRes> {
  return await post<CreateSpotRes>(node, '/spots', { spotRid, mode } satisfies CreateSpotReq);
}

export async function createActor(node: HttpClient, actorId: string, actorType: string, stateVersion: number): Promise<ActorCreateRes> {
  return await post(node, '/actors', { actorId, actorType, stateVersion } satisfies ActorCreateReq);
}

export async function joinActor(node: HttpClient, actorId: string, request: JoinTargetReq): Promise<JoinTargetRes> {
  return await post(node, `/actors/${actorId}/join`, {
    ...request,
    transferId: request.transferId ?? uniqueShort('transfer')
  } satisfies JoinTargetReq);
}

export async function probeActor(node: HttpClient, actorId: string, scenario: string, marker: string): Promise<ProbeRes> {
  return await post(node, `/actors/${actorId}/probe`, { scenario, marker } satisfies ProbeReq);
}

export async function sendHandoff(node: HttpClient, actorId: string, scenario: string, marker: string): Promise<void> {
  await post(node, `/actors/${actorId}/handoff`, { scenario, marker } satisfies ProbeReq);
}

export async function getRef(node: HttpClient, actorId: string): Promise<ActorRefSnapshotRes> {
  return await node.get(`/actors/${actorId}/ref`).fetch<ActorRefSnapshotRes>();
}

export async function waitSpotRef(node: HttpClient, spotRid: string, expectedNodeRid: string): Promise<void> {
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    const spot = await node.get(`/spots/${spotRid}/ref`).fetch<{ found: boolean }>();
    if (spot.found) return;
    await delay(100);
  }
  throw new Error(`Spot '${spotRid}' did not resolve while waiting for '${expectedNodeRid}'.`);
}

export async function getEvidence(node: HttpClient): Promise<readonly ActorEvidence[]> {
  return await node.get('/evidence').fetch<ActorEvidence[]>();
}

export async function waitEvidence(node: HttpClient, containsAll: readonly string[], timeoutMilliseconds = 15000): Promise<readonly ActorEvidence[]> {
  const entries = await post<ActorEvidence[]>(node, '/evidence/wait', {
    containsAll,
    timeoutMilliseconds
  } satisfies EvidenceWaitReq);
  for (const expected of containsAll) {
    require(entries.some((entry) => text(entry).includes(expected)), `Evidence missing '${expected}'.`);
  }
  return entries;
}

export async function post<T = unknown>(node: HttpClient, path: string, body: unknown): Promise<T> {
  return await node.post(path).body(body).fetch<T>();
}

export function assertOrder(entries: readonly ActorEvidence[], actorId: string, kinds: readonly string[]): void {
  const filtered = entries.filter((entry) => entry.actorId === actorId);
  let cursor = -1;
  for (const kind of kinds) {
    const next = filtered.findIndex((entry, index) => index > cursor && entry.kind === kind);
    require(next > cursor, `Expected '${kind}' after evidence index ${cursor}.`);
    cursor = next;
  }
}

export function assertValuesInOrder(
  entries: readonly ActorEvidence[],
  actorId: string,
  kind: string,
  values: readonly string[]
): void {
  const actual = entries
    .filter((entry) => entry.actorId === actorId && entry.kind === kind)
    .map((entry) => entry.value);
  require(
    values.every((value, index) => actual[index] === value),
    `Expected ${kind} values '${values.join(',')}', got '${actual.join(',')}'.`
  );
}

export function mergeEvidence(...groups: readonly (readonly ActorEvidence[])[]): readonly ActorEvidence[] {
  return groups.flat().sort((left, right) => {
    const byTime = BigInt(left.atNs) - BigInt(right.atNs);
    if (byTime < 0n) return -1;
    if (byTime > 0n) return 1;
    return left.sequence - right.sequence;
  });
}

export function has(entries: readonly ActorEvidence[], actorId: string, kind: string): boolean {
  return entries.some((entry) => entry.actorId === actorId && entry.kind === kind);
}

export function text(entry: ActorEvidence): string {
  return `${entry.scenario}|${entry.actorId}|${entry.kind}|${entry.value}|${entry.nodeRid}|transfer=${entry.transferId ?? '<none>'}`;
}

export function unique(prefix: string): string { return `${prefix}-${crypto.randomUUID().replaceAll('-', '')}`; }
export function uniqueShort(prefix: string): string { return `${prefix}-${crypto.randomUUID().slice(0, 8)}`; }
export function delay(ms: number): Promise<void> { return new Promise((resolve) => setTimeout(resolve, ms)); }
export async function isPending(promise: Promise<unknown>): Promise<boolean> {
  return await Promise.race([promise.then(() => false, () => false), delay(1).then(() => true)]);
}
export function require(condition: unknown, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

export function parseOptions(args: readonly string[]): ClientOptions {
  const values = new Map<string, string>();
  for (let index = 0; index < args.length; index += 2) {
    const value = args[index + 1];
    if (value === undefined) throw new Error(`Missing value for '${args[index]}'.`);
    values.set(args[index].replace(/^--/, ''), value);
  }
  const get = (key: string): string => {
    const value = values.get(key);
    if (value === undefined) throw new Error(`--${key} is required.`);
    return value;
  };
  return {
    nodeAUrl: get('node-a-url'),
    nodeBUrl: get('node-b-url'),
    sessionAStreamEndpoint: get('session-a-stream-endpoint'),
    sessionBStreamEndpoint: get('session-b-stream-endpoint'),
    scenario: values.get('scenario') ?? 'all'
  };
}

export async function closeScenarioClients(): Promise<void> {
  await Promise.allSettled([nodeA.close(), nodeB.close()]);
}
