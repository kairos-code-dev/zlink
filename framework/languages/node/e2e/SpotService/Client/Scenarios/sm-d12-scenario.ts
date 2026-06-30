import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import type {
  ActorPingRes,
  ActorPingReq,
  ActorPushNotify,
  ActorPushReq,
  AuthRes,
  AuthReq,
  SnapshotRes,
  SnapshotReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmD12(options: ClientOptions): Promise<void> {
  const actorId = 'actor-sm-d12-transfer';
  const first = createStreamClient(options.sessionAStreamEndpoint);
  let second: ReturnType<typeof createStreamClient> | undefined;

  await first.connect();
  try {
    await first
      .request({
        actorId,
        displayName: 'api transfer',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthRes>();

    const firstReply = decodeStreamReply<ActorPingRes>(await first
      .request({ value: 'before-transfer' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit());
    ensure(firstReply.actorId === actorId, 'SM-D12 first api actor mismatch.');
    ensure(firstReply.nodeRid === 'play-a', 'SM-D12 first api node mismatch.');
    ensure(firstReply.seen === 1, 'SM-D12 expected initial actor state.');

    await first.close();

    second = createStreamClient(options.sessionBStreamEndpoint);
    await second.connect();
    await second
      .request({
        actorId,
        displayName: 'api transfer',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthRes>();

    const snapshot = decodeStreamReply<SnapshotRes>(await second
      .request({ actorId } satisfies SnapshotReq)
      .packetName('SnapshotReq')
      .timeout(5000)
      .submit());
    ensure(snapshot.actorId === actorId, 'SM-D12 snapshot actor mismatch.');
    ensure(snapshot.seen === 1, 'SM-D12 actor state was not preserved across apis.');

    const pushed = second.waitFor<ActorPushNotify>('ActorPushNotify')
      .where((message) => message.payload.actorId === actorId)
      .timeout(10000)
      .submit();
    const resumed = decodeStreamReply<ActorPingRes>(await second
      .request({ value: 'after-transfer' } satisfies ActorPushReq)
      .packetName('ActorPushReq')
      .timeout(5000)
      .submit());
    const notify = await pushed;
    ensure(resumed.actorId === actorId, 'SM-D12 resumed actor mismatch.');
    ensure(resumed.nodeRid === 'play-a', 'SM-D12 resumed node mismatch.');
    ensure(resumed.seen === 2, 'SM-D12 resumed actor state mismatch.');
    ensure(notify.payload.actorId === actorId, 'SM-D12 resumed push actor mismatch.');
    ensure(notify.payload.value === 'after-transfer', 'SM-D12 resumed push value mismatch.');
    ensure(notify.payload.seen === 2, 'SM-D12 resumed push state mismatch.');
  } finally {
    await first.close().catch(() => undefined);
    await second?.close().catch(() => undefined);
  }

  console.log('scenario SM-D12 passed');
}

function createStreamClient(endpoint: string) {
  return zlinkStreamConnectorFactory.create({
    endpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    maxReceivedMessages: 1024,
    waitTimeoutMs: 10000
  });
}
