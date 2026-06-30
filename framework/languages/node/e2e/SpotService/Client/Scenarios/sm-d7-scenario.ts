import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import type {
  ActorPingRes,
  ActorPingReq,
  AuthRes,
  AuthReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmD7(options: ClientOptions): Promise<void> {
  const client = zlinkStreamConnectorFactory.create({
    endpoint: options.sessionAStreamEndpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    waitTimeoutMs: 10000
  });
  await client.connect();
  try {
    const auth = decodeStreamReply<AuthRes>(await client
      .request({
        actorId: 'actor-sm-d7',
        displayName: 'stream auth',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit());
    ensure(auth.actorId === 'actor-sm-d7', 'SM-D7 auth reply actor mismatch.');

    const reply = decodeStreamReply<ActorPingRes>(await client
      .request({ value: 'auth-ok' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit());
    ensure(reply.actorId === 'actor-sm-d7', 'SM-D7 relay actor mismatch.');
    ensure(reply.value === 'auth-ok', 'SM-D7 relay value mismatch.');
  } finally {
    await client.close();
  }

  console.log('scenario SM-D7 passed');
}
