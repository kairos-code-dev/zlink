import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import type {
  ActorPingRes,
  ActorPushNotify,
  ActorPushReq,
  AuthRes,
  AuthReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmD14(options: ClientOptions): Promise<void> {
  ensure(options.sessionATlsStreamEndpoint.length > 0, 'SM-D14 TLS stream endpoint is required.');

  const strict = zlinkStreamConnectorFactory.create({
    endpoint: options.sessionATlsStreamEndpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    connectTimeoutMs: 5000,
    requestTimeoutMs: 5000,
    waitTimeoutMs: 10000,
    skipServerCertificateValidation: false
  });
  let strictTlsRejected = false;
  try {
    await strict.connect();
  } catch {
    strictTlsRejected = true;
  } finally {
    await strict.close();
  }
  ensure(strictTlsRejected, 'SM-D14 expected strict TLS validation to reject the self-signed stream certificate.');

  const actorId = 'actor-sm-d14-tls';
  const tls = zlinkStreamConnectorFactory.create({
    endpoint: options.sessionATlsStreamEndpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    connectTimeoutMs: 5000,
    requestTimeoutMs: 5000,
    waitTimeoutMs: 10000,
    skipServerCertificateValidation: true
  });
  await tls.connect();
  try {
    const auth = decodeStreamReply<AuthRes>(await tls
      .request({
        actorId,
        displayName: 'stream tls',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit());
    ensure(auth.actorId === actorId && auth.nodeRid === 'play-a', 'SM-D14 auth reply mismatch.');

    const pushed = tls.waitFor<ActorPushNotify>('ActorPushNotify')
      .where((message) => message.payload.actorId === actorId)
      .timeout(10000)
      .submit();
    const reply = decodeStreamReply<ActorPingRes>(await tls
      .request({ value: 'tls-push' } satisfies ActorPushReq)
      .packetName('ActorPushReq')
      .timeout(5000)
      .submit());
    const notify = await pushed;

    ensure(reply.actorId === actorId, 'SM-D14 TLS actor reply mismatch.');
    ensure(reply.nodeRid === 'play-a', 'SM-D14 TLS actor node mismatch.');
    ensure(notify.payload.actorId === actorId, 'SM-D14 TLS push actor mismatch.');
    ensure(notify.payload.value === 'tls-push', 'SM-D14 TLS push payload mismatch.');
  } finally {
    await tls.close();
  }

  console.log('scenario SM-D14 passed');
}
