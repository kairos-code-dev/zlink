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

export async function runSmB7(options: ClientOptions): Promise<void> {
  const actorId = `actor-sm-b7-order-${Date.now()}`;
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
        displayName: 'order',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit());
    ensure(auth.actorId === actorId && auth.nodeRid === 'play-a', 'SM-B7 auth reply mismatch.');

    const first = decodeStreamReply<ActorPingReply>(await client
      .request({ value: 'order-1' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit());
    const second = decodeStreamReply<ActorPingReply>(await client
      .request({ value: 'order-2' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit());

    ensure(
      first.actorId === actorId && first.value === 'order-1' && first.seen === 1,
      'SM-B7 first actor request was not processed first.'
    );
    ensure(
      second.actorId === actorId && second.value === 'order-2' && second.seen === 2,
      'SM-B7 second actor request was not processed second.'
    );

    const expectedEvidence = [
      `actor-ping|rid=play-a|actor=${actorId}`,
      'value=order-2|seen=2'
    ];
    const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
      containsAll: expectedEvidence,
      timeoutMilliseconds: 10000
    } satisfies EvidenceWaitRequest);
    ensure(
      evidence.some((line) =>
        line.includes(`actor-ping|rid=play-a|actor=${actorId}`)
        && line.includes('value=order-2|seen=2')
      ),
      'SM-B7 evidence did not include ordered second request.'
    );
  } finally {
    await client.close();
  }

  console.log('scenario SM-B7 passed');
}
