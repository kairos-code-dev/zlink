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

export async function runSmB5(options: ClientOptions): Promise<void> {
  const actorId = `actor-sm-b5-missing-${Date.now()}`;
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
        displayName: 'missing handler actor',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit());
    ensure(auth.actorId === actorId && auth.nodeRid === 'play-a', 'SM-B5 auth reply mismatch.');

    let failed = false;
    try {
      await client
        .request({ value: 'missing-handler' } satisfies ActorPingReq)
        .packetName('MissingActorReq')
        .timeout(2000)
        .submit<ActorPingReply>();
    } catch {
      failed = true;
    }
    ensure(failed, 'SM-B5 expected missing actor handler request to fail.');

    const expectedEvidence = [
      'dispatch-error|surface=spotActor|kind=actorRequest|reason=handlerMissing|action=replyError|packet=MissingActorReq'
    ];
    const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
      containsAll: expectedEvidence,
      timeoutMilliseconds: 10000
    } satisfies EvidenceWaitRequest);
    ensure(
      expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
      'SM-B5 evidence mismatch.'
    );
  } finally {
    await client.close();
  }

  console.log('scenario SM-B5 passed');
}
