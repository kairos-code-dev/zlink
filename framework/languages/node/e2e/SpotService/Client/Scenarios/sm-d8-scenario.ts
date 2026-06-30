import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import type {
  ActorPingRes,
  ActorPingReq,
  AuthRes,
  AuthReq,
  SlowActorPingReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmD8(options: ClientOptions): Promise<void> {
  const actorId = 'actor-sm-d8-reconnect';
  const first = createStreamClient(options.sessionAStreamEndpoint);
  let second: ReturnType<typeof createStreamClient> | undefined;

  await first.connect();
  try {
    await first
      .request({
        actorId,
        displayName: 'stream reconnect',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthRes>();

    const pending = first
      .request({
        value: 'before-disconnect',
        delayMs: 1000
      } satisfies SlowActorPingReq)
      .packetName('SlowActorPingReq')
      .timeout(10000)
      .submit();
    await new Promise((resolve) => setTimeout(resolve, 100));
    await first.close();

    let pendingFailed = false;
    try {
      await pending;
    } catch {
      pendingFailed = true;
    }
    ensure(pendingFailed, 'SM-D8 expected pending request to fail after stream disconnect.');
    await new Promise((resolve) => setTimeout(resolve, 1200));

    second = createStreamClient(options.sessionAStreamEndpoint);
    await second.connect();
    await second
      .request({
        actorId,
        displayName: 'stream reconnect',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthRes>();

    const resumed = decodeStreamReply<ActorPingRes>(await second
      .request({ value: 'after-reconnect' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit());
    ensure(resumed.actorId === actorId, 'SM-D8 reconnected actor mismatch.');
    ensure(resumed.nodeRid === 'play-a', 'SM-D8 reconnected node mismatch.');
    ensure(resumed.value === 'after-reconnect', 'SM-D8 reconnected value mismatch.');
  } finally {
    await first.close().catch(() => undefined);
    await second?.close().catch(() => undefined);
  }

  console.log('scenario SM-D8 passed');
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
