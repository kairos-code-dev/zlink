import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import type {
  ActorPingReply,
  ActorPingReq,
  AuthReply,
  EvidenceWaitRequest,
  LeaveReply,
  LeaveReq,
  UserSpotAuthReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmG3(options: ClientOptions): Promise<void> {
  const actorCount = 2;
  const key = Date.now();
  const spotRid = `spot-sm-g3-${key}`;
  const actorIds = Array.from({ length: actorCount }, (_, index) => `actor-sm-g3-${key}-${index}`);
  const clients: Array<ReturnType<typeof createStreamClient>> = [];

  try {
    for (const actorId of actorIds) {
      const client = await connectAndAuth(options.sessionAStreamEndpoint, spotRid, actorId);
      clients.push(client);
    }

    await Promise.all(actorIds.map(async (actorId, index) => {
      const client = clients[index];
      const ping = decodeStreamReply<ActorPingReply>(await client
        .request({ value: actorId } satisfies ActorPingReq)
        .packetName('UserActorPingReq')
        .timeout(5000)
        .submit());
      ensure(ping.actorId === actorId, 'SM-G3 actor request target mismatch.');
      ensure(ping.nodeRid === 'play-a', 'SM-G3 actor request reached the wrong node.');

      const left = decodeStreamReply<LeaveReply>(await client
        .request({ actorId } satisfies LeaveReq)
        .packetName('LeaveReq')
        .timeout(5000)
        .submit());
      ensure(left.accepted && left.actorId === actorId, 'SM-G3 leave reply mismatch.');
    }));

    const expectedEvidence = actorIds.flatMap((actorId) => [
      `spot-actor-joined|rid=play-a|spot=${spotRid}|actor=${actorId}`,
      `spot-actor-left|rid=play-a|spot=${spotRid}|actor=${actorId}`
    ]);
    const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
      containsAll: expectedEvidence,
      timeoutMilliseconds: 10000
    } satisfies EvidenceWaitRequest);
    ensure(
      expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
      'SM-G3 expected concurrent join and leave evidence.'
    );
    for (const actorId of actorIds) {
      ensure(
        evidence.filter((line) =>
          line.includes(`spot-actor-joined|rid=play-a|spot=${spotRid}|actor=${actorId}`)).length === 1,
        `SM-G3 join evidence count mismatch for ${actorId}.`
      );
      ensure(
        evidence.filter((line) =>
          line.includes(`spot-actor-left|rid=play-a|spot=${spotRid}|actor=${actorId}`)).length === 1,
        `SM-G3 leave evidence count mismatch for ${actorId}.`
      );
    }
  } finally {
    await Promise.all(clients.map((client) => client.close().catch(() => undefined)));
  }

  console.log('scenario SM-G3 passed');
}

async function connectAndAuth(endpoint: string, spotRid: string, actorId: string) {
  const deadline = Date.now() + 15000;
  let last: unknown;
  while (Date.now() < deadline) {
    const client = createStreamClient(endpoint);
    try {
      await client.connect();
      await client
        .request({
          spotRid,
          actorId,
          displayName: actorId,
          nodeRid: 'play-a'
        } satisfies UserSpotAuthReq)
        .packetName('UserSpotAuthReq')
        .timeout(5000)
        .submit<AuthReply>();
      return client;
    } catch (error) {
      last = error;
      await client.close().catch(() => undefined);
      await new Promise((resolve) => setTimeout(resolve, 250));
    }
  }
  throw last instanceof Error
    ? new Error(`Actor auth did not become routable: ${actorId}. Last error: ${last.message}`)
    : new Error(`Actor auth did not become routable: ${actorId}.`);
}

function createStreamClient(endpoint: string) {
  return zlinkStreamConnectorFactory.create({
    endpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    waitTimeoutMs: 10000
  });
}
