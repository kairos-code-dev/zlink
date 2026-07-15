import {
  ZlinkStreamDispatchMode,
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  type ZlinkStreamConnector
} from '@zlink-systems/stream-connector';
import {
  ObservabilityOpsNames,
  type ActorCreateReq,
  type ActorCreateRes,
  type BindActorSessionReq,
  type BindActorSessionRes,
  type BoundPushNotify,
  type BoundPushReq,
  type BoundPushRes,
  type CreateSpotReq,
  type CreateSpotRes,
  type JoinTargetReq,
  type JoinTargetRes,
  type ProbeReq,
  type ProbeRes
} from '../../Shared/messages.js';
import {
  browserE2eArgs
} from '../../../browser-client-runtime';
import { ZLinkHttpClient, type ZLinkHttpClient as HttpClient } from '@zlink-systems/http-client';

export { ObservabilityOpsNames };

export interface ClientOptions {
  nodeAUrl: string;
  nodeBUrl: string;
  sessionAStreamEndpoint: string;
  sessionBStreamEndpoint: string;
  sessionUrl: string;
  workflowAUrl: string;
  workflowBUrl: string;
  logDir: string;
  c5Phase: string;
  scenario: string;
}

export const options = parseOptions(browserE2eArgs());
export const nodeA = ZLinkHttpClient.create(options.nodeAUrl).timeout(40000).build();
export const nodeB = ZLinkHttpClient.create(options.nodeBUrl).timeout(40000).build();
export const session = ZLinkHttpClient.create(options.sessionUrl).timeout(40000).build();
export const workflowA = ZLinkHttpClient.create(options.workflowAUrl).timeout(40000).build();
export const workflowB = ZLinkHttpClient.create(options.workflowBUrl).timeout(40000).build();

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
  } satisfies BindActorSessionReq).packetName(ObservabilityOpsNames.packetBindActor).submit<BindActorSessionRes>();
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
  const pushed = connector.waitFor<BoundPushNotify>(ObservabilityOpsNames.packetBoundNotify)
    .where((message) => message.payload.scenario === scenario && message.payload.marker === marker)
    .timeout(15000).submit();
  const reply = await connector.request({ scenario, marker } satisfies BoundPushReq)
    .packetName(ObservabilityOpsNames.packetBoundPush)
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
  const pushed = connector.waitFor<BoundPushNotify>(ObservabilityOpsNames.packetBoundNotify)
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

export async function post<T = unknown>(node: HttpClient, path: string, body: unknown): Promise<T> {
  return await node.post(path).body(body).fetch<T>();
}

export function unique(prefix: string): string { return `${prefix}-${crypto.randomUUID().replaceAll('-', '')}`; }
export function uniqueShort(prefix: string): string { return `${prefix}-${crypto.randomUUID().slice(0, 8)}`; }
export function delay(ms: number): Promise<void> { return new Promise((resolve) => setTimeout(resolve, ms)); }
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
    sessionUrl: get('session-url'),
    workflowAUrl: get('workflow-a-url'),
    workflowBUrl: get('workflow-b-url'),
    logDir: get('log-dir'),
    c5Phase: values.get('c5-phase') ?? 'sequential',
    scenario: values.get('scenario') ?? 'all'
  };
}

export async function closeScenarioClients(): Promise<void> {
  await Promise.allSettled([nodeA.close(), nodeB.close(), session.close(), workflowA.close(), workflowB.close()]);
}
