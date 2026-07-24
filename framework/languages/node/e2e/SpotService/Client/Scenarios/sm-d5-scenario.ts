// SM-D5: physical session disconnect의 Framework 자동 Actor 통지를 검증한다.
import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import type {
  AuthRes,
  AuthReq,
  EvidenceWaitReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

export async function runSmD5(options: ClientOptions): Promise<void> {
  const actorId = `actor-sm-d5-notified-${Date.now()}`;
  const client = zlinkStreamConnectorFactory.create({
    endpoint: options.sessionAStreamEndpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    waitTimeoutMs: 10000
  });
  await client.connect();
  try {
    await client
      .request({
        actorId,
        displayName: 'disconnect',
        nodeRid: 'session-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthRes>();
  } finally {
    await client.close();
  }

  const expectedEvidence = [`entry-disconnected|rid=session-a|actor=${actorId}`];
  const evidence = await postJson<string[]>(options.sessionAUrl, '/evidence/wait', {
    containsAll: expectedEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitReq);
  ensure(
    expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
    'SM-D5 expected Framework automatic bound actor disconnect notification.'
  );

  console.log('scenario SM-D5 passed');
}
