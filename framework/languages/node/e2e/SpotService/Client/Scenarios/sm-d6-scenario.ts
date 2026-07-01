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
  AuthReq,
  EvidenceWaitReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmD6(options: ClientOptions): Promise<void> {
  const bound = createStreamClient(options.sessionAStreamEndpoint);
  const unbound = createStreamClient(options.sessionBStreamEndpoint);
  let unboundTargetPushCount = 0;
  const unboundSubscription = unbound.on<ActorPushNotify>('ActorPushNotify', (message) => {
    if (message.payload.actorId === 'actor-sm-d6') {
      unboundTargetPushCount += 1;
    }
  });

  await bound.connect();
  await unbound.connect();
  try {
    await bound
      .request({
        actorId: 'actor-sm-d6',
        displayName: 'bound',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthRes>();
    await postJson<string[]>(options.playAUrl, '/evidence/wait', {
      containsAll: ['entry-joined|rid=play-a|actor=actor-sm-d6'],
      timeoutMilliseconds: 10000
    } satisfies EvidenceWaitReq);
    await unbound
      .request({
        actorId: 'actor-sm-d6-shadow',
        displayName: 'unbound',
        nodeRid: 'play-b'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthRes>();
    await postJson<string[]>(options.playBUrl, '/evidence/wait', {
      containsAll: ['entry-joined|rid=play-b|actor=actor-sm-d6-shadow'],
      timeoutMilliseconds: 10000
    } satisfies EvidenceWaitReq);

    const pushed = bound.waitFor<ActorPushNotify>('ActorPushNotify')
      .where((message) => message.payload.actorId === 'actor-sm-d6')
      .timeout(10000)
      .submit();
    await bound
      .request({ value: 'push-bound-only' } satisfies ActorPushReq)
      .packetName('ActorPushReq')
      .timeout(5000)
      .submit<ActorPingRes>();
    const notify = await pushed;
    ensure(notify.payload.actorId === 'actor-sm-d6', 'SM-D6 push actor mismatch.');
    ensure(notify.payload.value === 'push-bound-only', 'SM-D6 push value mismatch.');

    await new Promise((resolve) => setTimeout(resolve, 200));
    ensure(unboundTargetPushCount === 0, 'SM-D6 unbound session received target actor push.');
  } finally {
    unboundSubscription.dispose();
    await Promise.allSettled([bound.close(), unbound.close()]);
  }

  console.log('scenario SM-D6 passed');
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
