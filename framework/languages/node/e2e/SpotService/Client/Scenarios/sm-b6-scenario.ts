import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import type {
  AuthReply,
  AuthReq,
  EvidenceWaitRequest,
  LeaveReply,
  LeaveReq,
  UserSpotAuthReq
} from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { postJson } from '../Support/http-client';
import { ensure } from '../Support/scenario-assert';
import { decodeStreamReply } from '../Support/stream-reply';

export async function runSmB6(options: ClientOptions): Promise<void> {
  const suffix = Date.now();
  const spotRid = `spot-sm-b6-${suffix}`;
  const leaveActorId = `actor-sm-b6-left-${suffix}`;
  const disconnectActorId = `actor-sm-b6-disconnected-${suffix}`;

  const leaveClient = createStreamClient(options.sessionAStreamEndpoint);
  await leaveClient.connect();
  try {
    await leaveClient
      .request({
        spotRid,
        actorId: leaveActorId,
        displayName: leaveActorId,
        nodeRid: 'play-a'
      } satisfies UserSpotAuthReq)
      .packetName('UserSpotAuthReq')
      .timeout(5000)
      .submit<AuthReply>();

    const left = decodeStreamReply<LeaveReply>(await leaveClient
      .request({ actorId: leaveActorId } satisfies LeaveReq)
      .packetName('LeaveReq')
      .timeout(5000)
      .submit());
    ensure(left.accepted && left.actorId === leaveActorId, 'SM-B6 leave reply mismatch.');
  } finally {
    await leaveClient.close();
  }

  const expectedLeaveEvidence = [`spot-actor-left|rid=play-a|spot=${spotRid}|actor=${leaveActorId}`];
  const playAAfterLeave = await postJson<string[]>(options.playAUrl, '/evidence/wait', {
    containsAll: expectedLeaveEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitRequest);
  ensure(
    expectedLeaveEvidence.every((expected) => playAAfterLeave.some((line) => line.includes(expected))),
    'SM-B6 expected explicit leave evidence.'
  );
  ensure(
    playAAfterLeave.every((line) =>
      !line.includes(`spot-actor-disconnected|rid=play-a|spot=${spotRid}|actor=${leaveActorId}`)),
    'SM-B6 explicit leave incorrectly emitted disconnect evidence.'
  );

  const disconnectClient = createStreamClient(options.sessionAStreamEndpoint);
  await disconnectClient.connect();
  try {
    await disconnectClient
      .request({
        actorId: disconnectActorId,
        displayName: 'disconnect',
        nodeRid: 'session-a'
      } satisfies AuthReq)
      .packetName('AuthReq')
      .timeout(5000)
      .submit<AuthReply>();
  } finally {
    await disconnectClient.close();
  }

  const expectedDisconnectEvidence = [`entry-disconnected|rid=session-a|actor=${disconnectActorId}`];
  const sessionAfterDisconnect = await postJson<string[]>(options.sessionAUrl, '/evidence/wait', {
    containsAll: expectedDisconnectEvidence,
    timeoutMilliseconds: 10000
  } satisfies EvidenceWaitRequest);
  ensure(
    expectedDisconnectEvidence.every((expected) => sessionAfterDisconnect.some((line) => line.includes(expected))),
    'SM-B6 expected disconnect evidence.'
  );
  ensure(
    sessionAfterDisconnect.every((line) => !line.includes(`entry-left|rid=session-a|actor=${disconnectActorId}`)),
    'SM-B6 disconnect incorrectly emitted leave evidence.'
  );

  console.log('scenario SM-B6 passed');
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
