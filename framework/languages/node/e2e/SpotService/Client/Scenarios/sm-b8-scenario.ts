import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import type {
  AuthReply,
  AuthReq,
  DestroyActorReply,
  DestroyActorReq,
  EvidenceWaitRequest,
  SnapshotReply,
  SnapshotReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmB8(options: ClientOptions): Promise<void> {
  const actorId = `actor-sm-b8-destroy-${Date.now()}`;
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
        displayName: 'destroy',
        nodeRid: 'play-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit());
    ensure(auth.actorId === actorId && auth.nodeRid === 'play-a', 'SM-B8 auth reply mismatch.');

    const destroyed = decodeStreamReply<DestroyActorReply>(await client
      .request({ actorId } satisfies DestroyActorReq)
      .packetName('DestroyActorReq')
      .timeout(5000)
      .submit());
    ensure(
      destroyed.actorId === actorId && destroyed.destroyed,
      'SM-B8 destroy reply mismatch.'
    );

    let snapshotFailed = false;
    for (let attempt = 0; attempt < 10; attempt += 1) {
      try {
        await client
          .request({ actorId } satisfies SnapshotReq)
          .packetName('SnapshotReq')
          .timeout(1000)
          .submit<SnapshotReply>();
      } catch {
        snapshotFailed = true;
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 150));
    }
    ensure(snapshotFailed, 'SM-B8 expected request to destroyed actor to fail.');

    const evidence = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
      containsAll: [`actor-destroyed|rid=play-a|actor=${actorId}`],
      timeoutMilliseconds: 10000
    } satisfies EvidenceWaitRequest);
    ensure(
      evidence.some((line) => line.includes(`actor-destroyed|rid=play-a|actor=${actorId}`)),
      'SM-B8 expected actor destroy evidence.'
    );
    ensure(
      evidence.every((line) => !line.includes(`actor-destroy-failed|rid=play-a|actor=${actorId}`)),
      'SM-B8 actor destroy reported a failure.'
    );
  } finally {
    await client.close();
  }

  console.log('scenario SM-B8 passed');
}
