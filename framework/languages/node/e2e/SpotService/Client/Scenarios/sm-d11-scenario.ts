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
  ControlPingReply,
  ControlPingReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmD11(options: ClientOptions): Promise<void> {
  const stream = zlinkStreamConnectorFactory.create({
    endpoint: options.sessionAStreamEndpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    waitTimeoutMs: 10000
  });

  await stream.connect();
  try {
    const auth = decodeStreamReply<AuthReply>(await stream
      .request({
        actorId: 'actor-sm-d11',
        displayName: 'stream and channel',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit());
    ensure(auth.actorId === 'actor-sm-d11', 'SM-D11 auth reply actor mismatch.');

    const streamReply = decodeStreamReply<ActorPingReply>(await stream
      .request({ value: 'stream-side' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit());
    ensure(streamReply.actorId === 'actor-sm-d11', 'SM-D11 stream request actor mismatch.');
    ensure(streamReply.value === 'stream-side', 'SM-D11 stream reply value mismatch.');
  } finally {
    await stream.close();
  }

  const channelReply = await postJson<ControlPingReply>(options.sessionAUrl, '/channel/control-ping/play-a', {
    value: 'channel-side'
  } satisfies ControlPingReq);
  ensure(channelReply.nodeRid === 'play-a', 'SM-D11 channel request node mismatch.');
  ensure(channelReply.value === 'channel-side', 'SM-D11 channel reply value mismatch.');

  console.log('scenario SM-D11 passed');
}
