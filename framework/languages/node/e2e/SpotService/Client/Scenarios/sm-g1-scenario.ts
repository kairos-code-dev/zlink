import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import type { ActorPingReply, ActorPingReq, AuthReply, AuthReq } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmG1(options: ClientOptions): Promise<void> {
  const playA = createStreamClient(options.sessionAStreamEndpoint);
  const playB = createStreamClient(options.sessionBStreamEndpoint);
  let recovered: ReturnType<typeof createStreamClient> | undefined;

  await playA.connect();
  await playB.connect();
  try {
    await playA
      .request({
        actorId: 'actor-sm-g1-crash',
        displayName: 'crash-owner',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthReply>();
    await playB
      .request({
        actorId: 'actor-sm-g1-survivor',
        displayName: 'survivor',
        nodeRid: 'play-b'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthReply>();

    const beforeCrash = decodeStreamReply<ActorPingReply>(await playA
      .request({ value: 'before-crash' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit());
    ensure(beforeCrash.nodeRid === 'play-a', 'SM-G1 play-a actor setup mismatch.');

    const beforeSurvivor = decodeStreamReply<ActorPingReply>(await playB
      .request({ value: 'before-crash' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit());
    ensure(beforeSurvivor.nodeRid === 'play-b', 'SM-G1 play-b actor setup mismatch.');

    const crash = await fetch(`${options.playAUrl}/crash`, { method: 'POST' });
    ensure(crash.ok, `SM-G1 crash endpoint returned HTTP ${crash.status}.`);
    await new Promise((resolve) => setTimeout(resolve, 200));

    const afterCrashFailed = await fails(async () => {
      await playA
        .request({ value: 'after-crash' } satisfies ActorPingReq)
        .packetName('ActorPingReq')
        .timeout(1000)
        .submit();
    });
    ensure(afterCrashFailed, 'SM-G1 expected play-a actor request to fail after crash.');

    const survivor = decodeStreamReply<ActorPingReply>(await playB
      .request({ value: 'after-crash' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit());
    ensure(survivor.actorId === 'actor-sm-g1-survivor', 'SM-G1 survivor actor mismatch.');
    ensure(survivor.nodeRid === 'play-b', 'SM-G1 survivor node mismatch.');

    recovered = createStreamClient(options.sessionBStreamEndpoint);
    await recovered.connect();
    await recovered
      .request({
        actorId: 'actor-sm-g1-crash',
        displayName: 'recovered-on-play-b',
        nodeRid: 'play-b'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthReply>();
    const rebound = decodeStreamReply<ActorPingReply>(await recovered
      .request({ value: 'rebound' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit());
    ensure(rebound.actorId === 'actor-sm-g1-crash', 'SM-G1 rebound actor mismatch.');
    ensure(rebound.nodeRid === 'play-b', 'SM-G1 rebound node mismatch.');
  } finally {
    await playA.close().catch(() => undefined);
    await playB.close().catch(() => undefined);
    await recovered?.close().catch(() => undefined);
  }

  console.log('scenario SM-G1 passed');
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

async function fails(operation: () => Promise<void>): Promise<boolean> {
  try {
    await operation();
    return false;
  } catch {
    return true;
  }
}
