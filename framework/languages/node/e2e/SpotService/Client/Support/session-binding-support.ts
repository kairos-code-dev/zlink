import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode,
  type ZlinkStreamConnector
} from '@zlink-systems/stream-connector';
import type {
  ActorPingReq,
  ActorPingRes,
  AuthReq,
  AuthRes,
  EvidenceWaitReq
} from '../../Shared/messages';
import { SpotServiceNames } from '../../Shared/messages';
import { getJson, postJson } from '../../../http-client';
import { ensure } from './scenario-assert';

export function createSessionClient(endpoint: string): ZlinkStreamConnector {
  return zlinkStreamConnectorFactory.create({
    endpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    maxReceivedMessages: 1024,
    waitTimeoutMs: 10000,
    requestTimeoutMs: 5000
  });
}

export async function bindActor(
  client: ZlinkStreamConnector,
  actorId: string,
  nodeRid: string
): Promise<AuthRes> {
  const result = await client
    .request({ actorId, displayName: actorId, nodeRid } satisfies AuthReq)
    .packetName('AuthReq')
    .timeout(5000)
    .submit<AuthRes>();
  ensure(result.actorId === actorId, `Session binding returned '${result.actorId}' for '${actorId}'.`);
  return result;
}

export async function pingActor(
  client: ZlinkStreamConnector,
  actorId: string,
  value: string
): Promise<ActorPingRes> {
  return await client
    .request({ value } satisfies ActorPingReq)
    .packetName('ActorPingReq')
    .metadata(SpotServiceNames.actorIdMetadata, actorId)
    .timeout(5000)
    .submit<ActorPingRes>();
}

export async function expectStaleActorRoute(
  client: ZlinkStreamConnector,
  actorId: string,
  value: string
): Promise<void> {
  let rejected = false;
  try {
    await pingActor(client, actorId, value);
  } catch {
    rejected = true;
  }
  ensure(rejected, `Stale Session route for '${actorId}' unexpectedly reached the current binding.`);
}

export async function waitEvidence(
  baseUrl: string,
  ...containsAll: readonly string[]
): Promise<readonly string[]> {
  return await postJson<string[]>(baseUrl, '/evidence/wait', {
    containsAll,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
}

export async function getEvidence(baseUrl: string): Promise<readonly string[]> {
  return await getJson<string[]>(baseUrl, '/evidence');
}

export function countEvidence(entries: readonly string[], expected: string): number {
  return entries.filter((entry) => entry.includes(expected)).length;
}

export async function delay(milliseconds: number): Promise<void> {
  await new Promise((resolve) => setTimeout(resolve, milliseconds));
}
