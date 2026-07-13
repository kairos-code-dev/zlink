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
import { browserE2eFetch } from '../../../browser-client-runtime';

export async function runSmB2(options: ClientOptions): Promise<void> {
  const actorId = `actor-sm-b2-remote-${Date.now()}`;
  const client = zlinkStreamConnectorFactory.create({
    endpoint: options.sessionAStreamEndpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    waitTimeoutMs: 10000
  });
  await client.connect();
  try {
    const auth = await client
      .request({
        actorId,
        displayName: 'remote actor',
        nodeRid: 'play-b'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthRes>();
    ensure(auth.actorId === actorId && auth.nodeRid === 'play-b', 'SM-B2 auth reply mismatch.');

    const pingMsg = await client
      .request({ value: 'b2' } satisfies ActorPingReq)
      .packetName('ActorPingReq')
      .timeout(5000)
      .submit<ActorPingRes>();
    ensure(pingMsg.actorId === actorId, 'SM-B2 actor reply mismatch.');
    ensure(pingMsg.nodeRid === 'play-b', 'SM-B2 remote node mismatch.');

    const expectedEvidence = [
      `entry-created|rid=play-b|actor=${actorId}`,
      `entry-joined|rid=play-b|actor=${actorId}`
    ];
    const evidence = await waitForEvidence(options.playBUrl, expectedEvidence, 30000);
    ensure(
      expectedEvidence.every((expected) => evidence.some((line) => line.includes(expected))),
      'SM-B2 evidence mismatch.'
    );
  } finally {
    await client.close();
  }

  console.log('scenario SM-B2 passed');
}

async function waitForEvidence(baseUrl: string, containsAll: readonly string[], timeoutMs: number): Promise<string[]> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const response = await browserE2eFetch(`${baseUrl}/evidence`);
    if (!response.ok) {
      throw new Error(`GET /evidence failed: ${response.status} ${await response.text()}`);
    }
    const evidence = await response.json() as string[];
    if (containsAll.every((expected) => evidence.some((line) => line.includes(expected)))) {
      return evidence;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`Timed out waiting for SM-B2 evidence: ${containsAll.join(', ')}`);
}
