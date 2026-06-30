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
  ControlPingRes,
  ControlPingReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmD1(options: ClientOptions): Promise<void> {
  await waitForControlRoute(options.sessionAUrl, 'play-a');

  const actorId = 'actor-sm-d1';
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
        actorId,
        displayName: 'local relay',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit());
    ensure(auth.actorId === actorId && auth.nodeRid === 'play-a', 'SM-D1 auth reply mismatch.');

    const pushed = client.waitFor<ActorPushNotify>('ActorPushNotify')
      .where((message) => message.payload.actorId === actorId)
      .timeout(10000)
      .submit();
    const reply = decodeStreamReply<ActorPingRes>(await client
      .request({ value: 'push-local' } satisfies ActorPushReq)
      .packetName('ActorPushReq')
      .timeout(5000)
      .submit());
    const notify = await pushed;

    ensure(reply.actorId === actorId, 'SM-D1 actor reply mismatch.');
    ensure(reply.nodeRid === 'play-a', 'SM-D1 actor request reached the wrong node.');
    ensure(notify.payload.actorId === actorId, 'SM-D1 push actor mismatch.');
    ensure(notify.payload.value === 'push-local', 'SM-D1 push value mismatch.');
  } finally {
    await client.close();
  }

  console.log('scenario SM-D1 passed');
}

async function waitForControlRoute(sessionUrl: string, targetRid: string): Promise<void> {
  const deadline = Date.now() + 30000;
  let lastError: unknown;
  while (Date.now() < deadline) {
    try {
      const reply = await postJson<ControlPingRes>(sessionUrl, `/channel/control-pingMsg/${targetRid}`, {
        value: `sm-d1-${targetRid}-ready`
      } satisfies ControlPingReq);
      if (reply.nodeRid === targetRid) {
        return;
      }
    } catch (error) {
      lastError = error;
    }
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
  throw new Error(
    lastError instanceof Error
      ? `Control route did not become ready: ${targetRid}. Last error: ${lastError.message}`
      : `Control route did not become ready: ${targetRid}.`
  );
}
