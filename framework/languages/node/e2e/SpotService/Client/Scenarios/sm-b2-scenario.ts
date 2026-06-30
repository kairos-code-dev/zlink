import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import type {
  ActorPingReply,
  ActorPingReq,
  AuthReply,
  AuthReq,
  EvidenceWaitRequest
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmB2(options: ClientOptions): Promise<void> {
  const actorId = `actor-sm-b2-remote-${Date.now()}`;
  const client = zlinkStreamConnectorFactory.create({
    endpoint: options.sessionAStreamEndpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    waitTimeoutMs: 10000
  });
  await client.connect();
  try {
    const auth = decodeStreamReply<AuthReply>(await client
      .request({
        actorId,
        displayName: 'remote actor',
        nodeRid: 'play-b'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit());
    ensure(auth.actorId === actorId && auth.nodeRid === 'play-b', 'SM-B2 auth reply mismatch.');

    const ping = decodeStreamReply<ActorPingReply>(await client
      .request({ value: 'b2' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit());
    ensure(ping.actorId === actorId, 'SM-B2 actor reply mismatch.');
    ensure(ping.nodeRid === 'play-b', 'SM-B2 remote node mismatch.');

    const expectedEvidence = [
      `entry-created|rid=play-b|actor=${actorId}`,
      `entry-joined|rid=play-b|actor=${actorId}`
    ];
    const evidence = await postJson<string[]>(options.playBUrl, '/evidence/wait', {
      containsAll: expectedEvidence,
      timeoutMilliseconds: 10000
    } satisfies EvidenceWaitRequest);
    ensure(
      expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
      'SM-B2 evidence mismatch.'
    );
  } finally {
    await client.close();
  }

  console.log('scenario SM-B2 passed');
}
