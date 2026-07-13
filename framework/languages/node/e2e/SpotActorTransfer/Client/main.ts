import {
  ZlinkStreamDispatchMode,
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  type ZlinkStreamConnector
} from '@zlink-systems/stream-connector';
import {
  SpotActorTransferNames,
  type ActorCreateReq,
  type ActorCreateRes,
  type ActorEvidence,
  type ActorRefSnapshotRes,
  type BindActorSessionReq,
  type BindActorSessionRes,
  type BoundPushNotify,
  type BoundPushReq,
  type BoundPushRes,
  type CreateSpotReq,
  type CreateSpotRes,
  type EvidenceWaitReq,
  type GateReleaseRes,
  type JoinTargetReq,
  type JoinTargetRes,
  type ProbeReq,
  type ProbeRes
} from '../Shared/messages.js';
import {
  BrowserE2eHttpClientFactory,
  browserE2eArgs,
  runBrowserE2e,
  type BrowserE2eHttpClient as HttpClient
} from '../../browser-client-runtime';

interface ClientOptions {
  nodeAUrl: string;
  nodeBUrl: string;
  sessionAStreamEndpoint: string;
  sessionBStreamEndpoint: string;
  scenario: string;
}

const options = parseOptions(browserE2eArgs());
const nodeA = BrowserE2eHttpClientFactory.create(options.nodeAUrl).timeout(40000).build();
const nodeB = BrowserE2eHttpClientFactory.create(options.nodeBUrl).timeout(40000).build();

const scenarios: Record<string, () => Promise<void>> = {
  'ST-A1': runA1,
  'ST-A2': runA2,
  'ST-A3': runA3,
  'ST-B1': runB1,
  'ST-B2': runB2,
  'ST-B3': runB3,
  'ST-B4': runB4,
  'ST-C1': runC1,
  'ST-C2': runC2,
  'ST-C3': runC3,
  'ST-D1': runD1,
  'ST-D2': runD2,
  'ST-E1': runE1,
  'ST-E2': runE2,
  'ST-F1': runF1,
  'ST-F2': runF2,
  'ST-F3': runF3,
  'ST-F4': runF4,
  'ST-F5': runF5,
  'ST-F6': runF6
};

async function main(): Promise<void> {
  try {
    const selected = options.scenario === 'all'
      ? Object.keys(scenarios)
      : options.scenario.split(',').map((value) => value.trim()).filter(Boolean);
    for (const name of selected) {
      const scenario = scenarios[name];
      if (scenario === undefined) throw new Error(`Unknown scenario '${name}'.`);
      await scenario();
      console.log(`scenario ${name} passed`);
    }
    console.log('spot-actor-transfer e2e result=passed');
  } finally {
    await Promise.allSettled([nodeA.close(), nodeB.close()]);
  }
}

async function runA1(): Promise<void> {
  const actorId = unique('actor-local-ok');
  const spotRid = unique('spot-local-ok');
  await createSpot(nodeA, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 11);
  const join = await joinActor(nodeA, actorId, { scenario: 'ST-A1', targetSpotRid: spotRid });
  require(join.accepted, 'ST-A1 join was rejected.');
  const probe = await probeActor(nodeA, actorId, 'ST-A1', 'after-joined');
  require(probe.nodeRid === 'actor-a' && probe.spotRid === spotRid, 'ST-A1 packet did not reach the local user Spot.');
  const entries = await waitEvidence(nodeA, [
    `ST-A1|${actorId}|admission|spot=${spotRid}`,
    `transfer|${actorId}|leave|11`,
    `transfer|${actorId}|joined|${spotRid}:11`,
    `ST-A1|${actorId}|location_committed|node=actor-a|spot=${spotRid}`,
    `ST-A1|${actorId}|success_reply|${spotRid}`,
    `ST-A1|${actorId}|packet_handler|after-joined`
  ]);
  assertOrder(entries, actorId, ['admission', 'leave', 'joined', 'location_committed', 'success_reply', 'packet_handler']);
}

async function runA2(): Promise<void> {
  const actorId = unique('actor-local-reject');
  const spotRid = unique('spot-local-reject');
  await createSpot(nodeA, spotRid, 'reject');
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 12);
  const join = await joinActor(nodeA, actorId, { scenario: 'ST-A2', targetSpotRid: spotRid, expectedMode: 'reject' });
  require(!join.accepted, 'ST-A2 join should be rejected.');
  const entries = await waitEvidence(nodeA, [`ST-A2|${actorId}|admission|spot=${spotRid}`]);
  require(!has(entries, actorId, 'leave'), 'ST-A2 source leave side effect exists.');
  require(!has(entries, actorId, 'joined'), 'ST-A2 target joined side effect exists.');
  const actorRef = await getRef(nodeA, actorId);
  require(actorRef.nodeRid === 'actor-a', 'ST-A2 source ownership changed.');
  const entryProbe = await probeActor(nodeA, actorId, 'ST-A2', 'after-reject');
  require(entryProbe.nodeRid === 'actor-a', 'ST-A2 Entry Spot actor packet failed after rejection.');
  const afterProbe = await getEvidence(nodeA);
  require(has(afterProbe, actorId, 'entry_packet_handler'), 'ST-A2 Entry Spot handler evidence is missing.');
  require(!has(afterProbe, actorId, 'packet_handler'), 'ST-A2 target user Spot handled a rejected actor.');
}

async function runA3(): Promise<void> {
  const actorId = unique('actor-local-moving');
  const spotRid = unique('spot-local-moving');
  await createSpot(nodeA, spotRid, 'delay-joined');
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 13);
  const joinPromise = joinActor(nodeA, actorId, { scenario: 'ST-A3', targetSpotRid: spotRid });
  const waiting = await waitEvidence(nodeA, [
    `ST-A3|${actorId}|admission|spot=${spotRid}`,
    `ST-A3|${actorId}|joined_wait|${spotRid}`
  ]);
  require(!has(waiting, actorId, 'packet_handler'), 'ST-A3 packet ran before joined gate release.');
  const probePromise = probeActor(nodeA, actorId, 'ST-A3', 'during-joined-wait');
  await delay(300);
  require(await isPending(probePromise), 'ST-A3 packet completed while actor was moving.');
  const gate = await post<GateReleaseRes>(nodeA, `/joined-gates/${spotRid}/release`, {});
  require(gate.released, 'ST-A3 joined gate was already released.');
  require((await joinPromise).accepted, 'ST-A3 join failed.');
  const probe = await probePromise;
  require(probe.spotRid === spotRid, 'ST-A3 queued packet did not resume on target.');
}

async function runB1(): Promise<void> {
  await runRemoteTransfer('ST-B1', unique('actor-remote-ok'), SpotActorTransferNames.actorTypeStateful, 21, true);
}

async function runB2(): Promise<void> {
  const actorId = unique('actor-cleanup-after-success');
  const spotRid = unique('spot-cleanup-after-success');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 22);
  require((await joinActor(nodeA, actorId, { scenario: 'ST-B2', targetSpotRid: spotRid })).accepted, 'ST-B2 join failed.');
  await post(nodeA, '/shutdown', {});
  await delay(1500);
  const probe = await probeActor(nodeB, actorId, 'ST-B2', 'after-source-cleanup-loss');
  require(probe.nodeRid === 'actor-b' && probe.stateVersion === 22, 'ST-B2 target ownership was lost.');
}

async function runB3(): Promise<void> {
  const actorId = unique('actor-no-adapter');
  const spotRid = unique('spot-no-adapter');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeNoAdapter, 31);
  require((await joinActor(nodeA, actorId, { scenario: 'ST-B3', targetSpotRid: spotRid })).accepted, 'ST-B3 join failed.');
  const probe = await probeActor(nodeB, actorId, 'ST-B3', 'after-default-empty-transfer');
  require(probe.stateVersion === 0, 'ST-B3 default target state must be empty/default.');
  const source = await waitEvidence(nodeA, [
    `transfer|${actorId}|transfer_out_empty_default|no-adapter`,
    `ST-B3|${actorId}|commit_request|after-source-leave`,
    `ST-B3|${actorId}|commit_ack|${spotRid}`,
    `ST-B3|${actorId}|success_reply|${spotRid}`
  ]);
  const target = await waitEvidence(nodeB, [
    `ST-B3|${actorId}|admission|spot=${spotRid}`,
    `transfer|${actorId}|transfer_in_empty_default|actor-factory`,
    `transfer|${actorId}|joined|${spotRid}:0`,
    `ST-B3|${actorId}|location_committed|node=actor-b|spot=${spotRid}`
  ]);
  assertOrder(mergeEvidence(source, target), actorId, [
    'admission', 'transfer_out_empty_default', 'leave', 'commit_request',
    'transfer_in_empty_default', 'joined', 'location_committed', 'commit_ack', 'success_reply'
  ]);
}

async function runB4(): Promise<void> {
  const actorId = unique('actor-empty-state');
  const spotRid = unique('spot-empty-state');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeEmptyState, 41);
  require((await joinActor(nodeA, actorId, { scenario: 'ST-B4', targetSpotRid: spotRid })).accepted, 'ST-B4 join failed.');
  const probe = await probeActor(nodeB, actorId, 'ST-B4', 'after-empty-state-transfer');
  require(probe.stateVersion === 41, 'ST-B4 domain state was not loaded after joined.');
  const source = await waitEvidence(nodeA, [
    `transfer|${actorId}|transfer_out_empty|custom-adapter`,
    `ST-B4|${actorId}|commit_request|after-source-leave`,
    `ST-B4|${actorId}|commit_ack|${spotRid}`,
    `ST-B4|${actorId}|success_reply|${spotRid}`
  ]);
  const target = await waitEvidence(nodeB, [
    `ST-B4|${actorId}|admission|spot=${spotRid}`,
    `transfer|${actorId}|transfer_in_empty|custom-adapter`,
    `transfer|${actorId}|joined|${spotRid}:0`,
    `transfer|${actorId}|domain_state_loaded|${actorId}`,
    `ST-B4|${actorId}|location_committed|node=actor-b|spot=${spotRid}`
  ]);
  assertOrder(mergeEvidence(source, target), actorId, [
    'admission', 'transfer_out_empty', 'leave', 'commit_request', 'transfer_in_empty',
    'joined', 'domain_state_loaded', 'location_committed', 'commit_ack', 'success_reply'
  ]);
}

async function runC1(): Promise<void> {
  const actorId = unique('actor-source-down-before-commit');
  const spotRid = unique('spot-source-down-before-commit');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 62);
  const join = joinActor(nodeA, actorId, { scenario: 'ST-C1', targetSpotRid: spotRid }).catch(() => undefined);
  await waitEvidence(nodeB, [`ST-C1|${actorId}|admission|spot=${spotRid}`]);
  await waitEvidence(nodeA, [`ST-C1|${actorId}|before_commit_gate|62`]);
  await post(nodeA, '/shutdown', {});
  await join;
  await delay(31_000);
  const entries = await getEvidence(nodeB);
  require(!has(entries, actorId, 'joined'), 'ST-C1 target joined after source died before commit.');
  require(!has(entries, actorId, 'transfer_in'), 'ST-C1 transferIn ran without commit.');
}

async function runC2(): Promise<void> {
  const actorId = uniqueShort('c2');
  const spotRid = unique('spot-source-down-after-commit');
  await createSpot(nodeB, spotRid);
  const source = await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 61);
  const transferId = uniqueShort('transfer');
  const connector = await connectAndBind(options.sessionBStreamEndpoint, 'ST-C2', source, transferId);
  try {
    await assertBoundPush(connector, nodeA, actorId, 'ST-C2', 'bound-before-transfer', 'actor-a');
    require((await joinActor(nodeA, actorId, { scenario: 'ST-C2', targetSpotRid: spotRid, transferId })).accepted, 'ST-C2 join failed.');
    const before = await getRef(nodeB, actorId);
    await assertHttpBoundPush(connector, nodeB, actorId, 'ST-C2', 'bound-after-commit', 'actor-b');
    await post(nodeA, '/shutdown', {});
    await delay(1500);
    const after = await getRef(nodeB, actorId);
    require(after.nodeRid === 'actor-b' && after.generation === before.generation, 'ST-C2 target generation changed.');
    await assertHttpBoundPush(connector, nodeB, actorId, 'ST-C2', 'bound-after-source-down', 'actor-b');
  } finally {
    await connector.close();
  }
}

async function runC3(): Promise<void> {
  await assertSourceFailure('transfer-out', SpotActorTransferNames.actorTypeFailTransferOut, 'transfer_out_failed');
  await assertSourceFailure('leave', SpotActorTransferNames.actorTypeFailLeave, 'leave_failed');

  const transferInId = unique('actor-fail-transfer-in');
  const transferInSpot = unique('spot-fail-transfer-in');
  await createSpot(nodeB, transferInSpot);
  await createActor(nodeA, transferInId, SpotActorTransferNames.actorTypeFailTransferIn, 73);
  require(!(await joinActor(nodeA, transferInId, { scenario: 'ST-C3', targetSpotRid: transferInSpot })).accepted, 'ST-C3 transferIn failure returned success.');
  const targetAfterTransferIn = await getEvidence(nodeB);
  require(has(targetAfterTransferIn, transferInId, 'transfer_in_failed'), 'ST-C3 transferIn failure evidence missing.');
  require(!has(targetAfterTransferIn, transferInId, 'joined'), 'ST-C3 joined ran after transferIn failure.');

  const joinedId = unique('actor-fail-joined');
  const joinedSpot = unique('spot-fail-joined');
  await createSpot(nodeB, joinedSpot, 'fail-joined');
  await createActor(nodeA, joinedId, SpotActorTransferNames.actorTypeStateful, 74);
  require(!(await joinActor(nodeA, joinedId, { scenario: 'ST-C3', targetSpotRid: joinedSpot })).accepted, 'ST-C3 joined failure returned success.');
  const targetAfterJoined = await getEvidence(nodeB);
  require(has(targetAfterJoined, joinedId, 'joined_failed'), 'ST-C3 joined failure evidence missing.');
}

async function runD1(): Promise<void> {
  const actorId = unique('actor-location-delay');
  const spotRid = unique('spot-location-delay');
  await createSpot(nodeB, spotRid, 'delay-joined');
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 81);
  const join = joinActor(nodeA, actorId, { scenario: 'ST-D1', targetSpotRid: spotRid });
  await waitEvidence(nodeB, [`ST-D1|${actorId}|joined_wait|${spotRid}`]);
  const pending = await getRef(nodeA, actorId);
  require(pending.nodeRid === 'actor-a', 'ST-D1 target location became public before joined completed.');
  await post(nodeB, `/joined-gates/${spotRid}/release`, {});
  require((await join).accepted, 'ST-D1 join failed.');
  const committed = await getRef(nodeB, actorId);
  require(committed.nodeRid === 'actor-b', 'ST-D1 committed target location is missing.');
}

async function runD2(): Promise<void> {
  const actorId = unique('actor-stale-release');
  const spotRid = unique('spot-stale-release');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 82);
  require((await joinActor(nodeA, actorId, { scenario: 'ST-D2', targetSpotRid: spotRid })).accepted, 'ST-D2 join failed.');
  const before = await getRef(nodeB, actorId);
  await delay(1000);
  const after = await getRef(nodeB, actorId);
  require(after.nodeRid === 'actor-b' && after.generation === before.generation, 'ST-D2 stale source release removed target generation.');
  require((await probeActor(nodeB, actorId, 'ST-D2', 'after-stale-release')).nodeRid === 'actor-b', 'ST-D2 follow-up route failed.');
}

async function runE1(): Promise<void> {
  const actorId = uniqueShort('e1');
  const spotRid = unique('spot-bound-transfer');
  await createSpot(nodeB, spotRid);
  const source = await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 91);
  const transferId = uniqueShort('transfer');
  const connector = await connectAndBind(options.sessionAStreamEndpoint, 'ST-E1', source, transferId);
  try {
    await assertBoundPush(connector, nodeA, actorId, 'ST-E1', 'before-transfer', 'actor-a');
    require((await joinActor(nodeA, actorId, { scenario: 'ST-E1', targetSpotRid: spotRid, transferId })).accepted, 'ST-E1 join failed.');
    await assertHttpBoundPush(connector, nodeB, actorId, 'ST-E1', 'after-transfer', 'actor-b');
  } finally {
    await connector.close();
  }
}

async function runE2(): Promise<void> {
  const actorId = uniqueShort('e2');
  const spotRid = unique('spot-bound-failed-transfer');
  await createSpot(nodeB, spotRid);
  const source = await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeFailTransferOut, 92);
  const transferId = uniqueShort('transfer');
  const connector = await connectAndBind(options.sessionAStreamEndpoint, 'ST-E2', source, transferId);
  try {
    require(!(await joinActor(nodeA, actorId, { scenario: 'ST-E2', targetSpotRid: spotRid, transferId })).accepted, 'ST-E2 failed transfer returned success.');
    await assertBoundPush(connector, nodeA, actorId, 'ST-E2', 'after-failed-transfer', 'actor-a');
    const targetEvidence = await getEvidence(nodeB);
    require(!has(targetEvidence, actorId, 'bound_push'), 'ST-E2 target bound route was exposed.');
  } finally {
    await connector.close();
  }

  const rollbackActorId = uniqueShort('e2-joined');
  const rollbackSpotRid = unique('spot-bound-joined-failure');
  await createSpot(nodeB, rollbackSpotRid, 'fail-joined');
  const rollbackSource = await createActor(nodeA, rollbackActorId, SpotActorTransferNames.actorTypeStateful, 93);
  const rollbackTransferId = uniqueShort('transfer');
  const rollbackConnector = await connectAndBind(
    options.sessionAStreamEndpoint,
    'ST-E2',
    rollbackSource,
    rollbackTransferId
  );
  try {
    require(!(
      await joinActor(nodeA, rollbackActorId, {
        scenario: 'ST-E2',
        targetSpotRid: rollbackSpotRid,
        transferId: rollbackTransferId
      })
    ).accepted, 'ST-E2 joined failure returned success.');
    let targetPushFailed = false;
    try {
      await post(nodeB, `/actors/${rollbackActorId}/bound-push`, {
        scenario: 'ST-E2',
        marker: 'after-joined-failure'
      } satisfies BoundPushReq);
    } catch {
      targetPushFailed = true;
    }
    require(targetPushFailed, 'ST-E2 rolled-back target actor remained dispatchable.');
    require(!has(await getEvidence(nodeB), rollbackActorId, 'bound_push'), 'ST-E2 rolled-back target exposed a bound route.');
  } finally {
    await rollbackConnector.close();
  }
}

async function runF1(): Promise<void> {
  const actorId = unique('actor-handoff-gate-f1');
  const spotRid = unique('spot-handoff-order');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 101);
  const join = joinActor(nodeA, actorId, { scenario: 'ST-F1', targetSpotRid: spotRid });
  await waitEvidence(nodeA, [`ST-F1|${actorId}|before_commit_gate|101`]);
  for (const marker of ['P1', 'P2', 'P3']) await sendHandoff(nodeA, actorId, 'ST-F1', marker);
  await post(nodeA, `/transfer-gates/${actorId}/release`, {});
  require((await join).accepted, 'ST-F1 join failed.');
  const targetEntries = await waitEvidence(nodeB, ['P1', 'P2', 'P3'].map(
    (marker) => `ST-F1|${actorId}|packet_handler|${marker}`
  ));
  assertOrder(targetEntries, actorId, ['packet_handler', 'packet_handler', 'packet_handler']);
  require(!has(await getEvidence(nodeA), actorId, 'packet_handler'), 'ST-F1 source dispatched moving packets.');
}

async function runF2(): Promise<void> {
  const actorId = unique('actor-handoff-gate-f2');
  const spotRid = unique('spot-handoff-overtake');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 102);
  const join = joinActor(nodeA, actorId, { scenario: 'ST-F2', targetSpotRid: spotRid });
  await waitEvidence(nodeA, [`ST-F2|${actorId}|before_commit_gate|102`]);
  await sendHandoff(nodeA, actorId, 'ST-F2', 'B1');
  await sendHandoff(nodeA, actorId, 'ST-F2', 'B2');
  await post(nodeA, `/transfer-gates/${actorId}/release`, {});
  require((await join).accepted, 'ST-F2 join failed.');
  await sendHandoff(nodeB, actorId, 'ST-F2', 'D1');
  const entries = await waitEvidence(nodeB, [
    `ST-F2|${actorId}|backlog_enqueued|0`,
    `ST-F2|${actorId}|packet_handler|B1`,
    `ST-F2|${actorId}|backlog_enqueued|1`,
    `ST-F2|${actorId}|packet_handler|B2`,
    `ST-F2|${actorId}|location_committed|node=actor-b|spot=${spotRid}`,
    `ST-F2|${actorId}|packet_handler|D1`
  ]);
  assertOrder(entries, actorId, [
    'backlog_enqueued',
    'packet_handler',
    'backlog_enqueued',
    'packet_handler',
    'location_committed',
    'packet_handler'
  ]);
}

async function runF3(): Promise<void> {
  const actorId = uniqueShort('actor-handoff-gate-f3');
  const spotRid = unique('spot-handoff-bound');
  await createSpot(nodeB, spotRid);
  const source = await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 103);
  const connector = await connectAndBind(options.sessionAStreamEndpoint, 'ST-F3', source, uniqueShort('transfer'));
  try {
    const join = joinActor(nodeA, actorId, { scenario: 'ST-F3', targetSpotRid: spotRid });
    await waitEvidence(nodeA, [`ST-F3|${actorId}|before_commit_gate|103`]);
    await connector.send({ scenario: 'ST-F3', marker: 'S1' } satisfies ProbeReq)
      .packetName(SpotActorTransferNames.packetHandoff).submit();
    await connector.send({ scenario: 'ST-F3', marker: 'S2' } satisfies ProbeReq)
      .packetName(SpotActorTransferNames.packetHandoff).submit();
    await post(nodeA, `/transfer-gates/${actorId}/release`, {});
    await connector.send({ scenario: 'ST-F3', marker: 'S3' } satisfies ProbeReq)
      .packetName(SpotActorTransferNames.packetHandoff).submit();
    await connector.send({ scenario: 'ST-F3', marker: 'S4' } satisfies ProbeReq)
      .packetName(SpotActorTransferNames.packetHandoff).submit();
    require((await join).accepted, 'ST-F3 join failed.');
    const entries = await waitEvidence(nodeB, ['S1', 'S2', 'S3', 'S4'].map(
      (marker) => `ST-F3|${actorId}|packet_handler|${marker}`
    ));
    assertOrder(entries, actorId, ['packet_handler', 'packet_handler', 'packet_handler', 'packet_handler']);
  } finally {
    await connector.close();
  }
}

async function runF4(): Promise<void> {
  const actorId = unique('actor-straggler');
  const spotRid = unique('spot-straggler');
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 104);
  await getRef(nodeA, actorId);
  require((await joinActor(nodeA, actorId, { scenario: 'ST-F4', targetSpotRid: spotRid })).accepted, 'ST-F4 join failed.');
  await waitEvidence(nodeA, [`ST-F4|${actorId}|forwarding_mapped|500`]);
  await post(nodeA, `/actors/${actorId}/probe-stale`, { scenario: 'ST-F4', marker: 'G1' } satisfies ProbeReq);
  await waitEvidence(nodeB, [`ST-F4|${actorId}|packet_handler|G1`]);
  await delay(700);
  let failed = false;
  try {
    await post(nodeA, `/actors/${actorId}/probe-stale`, { scenario: 'ST-F4', marker: 'G2' } satisfies ProbeReq);
  } catch {
    failed = true;
  }
  require(failed, 'ST-F4 old ref packet after cutoff did not fail fast.');
  const source = await waitEvidence(nodeA, [
    `ST-F4|${actorId}|straggler_forward|`,
    `ST-F4|${actorId}|mapping_evicted|`,
    `ST-F4|${actorId}|stale_fail_fast|`
  ]);
  require(source.length > 0, 'ST-F4 forwarding evidence missing.');
  require(
    !(await getEvidence(nodeB)).some((entry) => entry.actorId === actorId && entry.value === 'G2'),
    'ST-F4 cutoff packet reached target.'
  );
}

async function runF5(): Promise<void> {
  const actorId = unique('actor-map-chain');
  const spotB = unique('spot-map-chain-b');
  const spotA = unique('spot-map-chain-a');
  await createSpot(nodeB, spotB);
  await createSpot(nodeA, spotA);
  await createActor(nodeA, actorId, SpotActorTransferNames.actorTypeStateful, 105);
  await getRef(nodeA, actorId);
  require((await joinActor(nodeA, actorId, { scenario: 'ST-F5', targetSpotRid: spotB })).accepted, 'ST-F5 first join failed.');
  require((await joinActor(nodeB, actorId, { scenario: 'ST-F5', targetSpotRid: spotA })).accepted, 'ST-F5 chained join failed.');
  const response = await post<ProbeRes>(nodeA, `/actors/${actorId}/probe-stale`, {
    scenario: 'ST-F5',
    marker: 'chain'
  } satisfies ProbeReq);
  require(response.nodeRid === 'actor-a', 'ST-F5 chained straggler did not reach final target.');
  await waitEvidence(nodeA, [
    `ST-F5|${actorId}|forwarding_mapped|500`,
    `ST-F5|${actorId}|straggler_forward|`
  ]);
  await waitEvidence(nodeB, [
    `ST-F5|${actorId}|forwarding_mapped|500`,
    `ST-F5|${actorId}|straggler_forward|`
  ]);
  await delay(700);
  await waitEvidence(nodeA, [`ST-F5|${actorId}|mapping_evicted|`]);
  await waitEvidence(nodeB, [`ST-F5|${actorId}|mapping_evicted|`]);
}

async function runF6(): Promise<void> {
  const replyActorId = unique('actor-handoff-gate-f6-reply');
  const replySpotRid = unique('spot-handoff-request-reply');
  await createSpot(nodeB, replySpotRid);
  await createActor(nodeA, replyActorId, SpotActorTransferNames.actorTypeStateful, 106);
  const replyJoin = joinActor(nodeA, replyActorId, {
    scenario: 'ST-F6',
    targetSpotRid: replySpotRid
  });
  await waitEvidence(nodeA, [`ST-F6|${replyActorId}|before_commit_gate|106`]);
  const inFlightReply = post<ProbeRes>(nodeA, `/actors/${replyActorId}/probe`, {
    scenario: 'ST-F6',
    marker: 'R1',
    requestTimeoutMs: 5000
  } satisfies ProbeReq);
  await waitEvidence(nodeA, [`ST-F6|${replyActorId}|handoff_backlog|0`]);
  const requestFrameEvidence = await waitEvidence(nodeA, [
    `ST-F6|${replyActorId}|handoff_request_frame|index=0|requestSeq=`
  ]);
  const requestFrame = requestFrameEvidence.find(
    (entry) => entry.actorId === replyActorId && entry.kind === 'handoff_request_frame'
  );
  require(
    requestFrame !== undefined && /requestSeq=\d+\|flags=[1-9]\d*/.test(requestFrame.value),
    'ST-F6 handoff evidence did not preserve request id and flags.'
  );
  await post(nodeA, `/transfer-gates/${replyActorId}/release`, {});
  require((await replyJoin).accepted, 'ST-F6 reply-correlation join failed.');
  const reply = await inFlightReply;
  require(
    reply.marker === 'R1' && reply.actorId === replyActorId && reply.nodeRid === 'actor-b',
    'ST-F6 in-flight request reply did not correlate to the original caller.'
  );
  const replyEvidence = await waitEvidence(nodeB, [
    `ST-F6|${replyActorId}|packet_handler|R1`,
    `ST-F6|${replyActorId}|request_reply|R1`
  ]);
  require(
    replyEvidence.filter((entry) => entry.actorId === replyActorId && entry.kind === 'request_reply').length === 1,
    'ST-F6 correlated request produced a duplicate reply.'
  );

  const timeoutActorId = unique('actor-handoff-gate-f6-timeout');
  const timeoutSpotRid = unique('spot-handoff-request-timeout');
  await createSpot(nodeB, timeoutSpotRid);
  await createActor(nodeA, timeoutActorId, SpotActorTransferNames.actorTypeStateful, 107);
  const timeoutJoin = joinActor(nodeA, timeoutActorId, {
    scenario: 'ST-F6',
    targetSpotRid: timeoutSpotRid
  });
  await waitEvidence(nodeA, [`ST-F6|${timeoutActorId}|before_commit_gate|107`]);
  const timedRequest = post<ProbeRes>(nodeA, `/actors/${timeoutActorId}/probe`, {
    scenario: 'ST-F6',
    marker: 'late',
    delayMs: 300,
    requestTimeoutMs: 100
  } satisfies ProbeReq);
  await waitEvidence(nodeA, [`ST-F6|${timeoutActorId}|handoff_backlog|0`]);
  await post(nodeA, `/transfer-gates/${timeoutActorId}/release`, {});
  let timedOut = false;
  try {
    await timedRequest;
  } catch {
    timedOut = true;
  }
  require(timedOut, 'ST-F6 delayed in-flight request did not use the caller request timeout.');
  require((await timeoutJoin).accepted, 'ST-F6 timeout join failed.');
  await waitEvidence(nodeB, [`ST-F6|${timeoutActorId}|request_reply|late`]);
  const afterLateReply = await probeActor(nodeB, timeoutActorId, 'ST-F6', 'after-late');
  require(afterLateReply.marker === 'after-late', 'ST-F6 late reply disrupted the next request.');
}

async function runRemoteTransfer(scenario: string, actorId: string, actorType: string, stateVersion: number, stateful: boolean): Promise<void> {
  const spotRid = unique('spot-remote');
  await createSpot(nodeB, spotRid);
  await waitSpotRef(nodeA, spotRid, 'actor-b');
  const sourceActor = await createActor(nodeA, actorId, actorType, stateVersion);
  require(sourceActor.nodeRid === 'actor-a', `${scenario} source actor was created on '${sourceActor.nodeRid}'.`);
  require((await joinActor(nodeA, actorId, { scenario, targetSpotRid: spotRid })).accepted, `${scenario} join failed.`);
  const probe = await probeActor(nodeB, actorId, scenario, 'after-transfer');
  require(probe.nodeRid === 'actor-b' && (!stateful || probe.stateVersion === stateVersion), `${scenario} target state mismatch.`);
  const source = await waitEvidence(nodeA, [
    `transfer|${actorId}|transfer_out|${stateVersion}`,
    `transfer|${actorId}|leave|${stateVersion}`,
    `${scenario}|${actorId}|commit_request|after-source-leave`,
    `${scenario}|${actorId}|commit_ack|${spotRid}`,
    `${scenario}|${actorId}|success_reply|${spotRid}`
  ]);
  const target = await waitEvidence(nodeB, [
    `${scenario}|${actorId}|admission|spot=${spotRid}`,
    `transfer|${actorId}|transfer_in|${stateVersion}`,
    `transfer|${actorId}|joined|${spotRid}:${stateVersion}`,
    `${scenario}|${actorId}|location_committed|node=actor-b|spot=${spotRid}`
  ]);
  require(source.length > 0 && target.length > 0, `${scenario} transfer evidence missing.`);
  assertOrder(mergeEvidence(source, target), actorId, [
    'admission',
    'transfer_out',
    'leave',
    'commit_request',
    'transfer_in',
    'joined',
    'location_committed',
    'commit_ack',
    'success_reply'
  ]);
}

async function assertSourceFailure(label: string, actorType: string, expectedKind: string): Promise<void> {
  const actorId = unique(`actor-fail-${label}`);
  const spotRid = unique(`spot-fail-${label}`);
  await createSpot(nodeB, spotRid);
  await createActor(nodeA, actorId, actorType, 70);
  require(!(await joinActor(nodeA, actorId, { scenario: 'ST-C3', targetSpotRid: spotRid })).accepted, `ST-C3 ${label} failure returned success.`);
  const source = await getEvidence(nodeA);
  const target = await getEvidence(nodeB);
  require(has(source, actorId, expectedKind), `ST-C3 ${label} evidence missing.`);
  require(!has(target, actorId, 'joined'), `ST-C3 target joined after ${label} failure.`);
}

async function connectAndBind(
  endpoint: string,
  scenario: string,
  actor: ActorCreateRes,
  transferId: string
): Promise<ZlinkStreamConnector> {
  const connector = zlinkStreamConnectorFactory.create({
    endpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    waitTimeoutMs: 15000,
    requestTimeoutMs: 10000
  });
  await connector.connect();
  const bound = await connector.request({
    scenario,
    actorId: actor.actorId,
    nodeRid: actor.nodeRid,
    generation: actor.generation,
    transferId
  } satisfies BindActorSessionReq).packetName(SpotActorTransferNames.packetBindActor).submit<BindActorSessionRes>();
  require(bound.actorId === actor.actorId, `${scenario} session bind mismatch.`);
  await delay(500);
  return connector;
}

async function assertBoundPush(
  connector: ZlinkStreamConnector,
  _node: HttpClient,
  actorId: string,
  scenario: string,
  marker: string,
  expectedNode: string
): Promise<void> {
  const pushed = connector.waitFor<BoundPushNotify>(SpotActorTransferNames.packetBoundNotify)
    .where((message) => message.payload.scenario === scenario && message.payload.marker === marker)
    .timeout(15000).submit();
  const reply = await connector.request({ scenario, marker } satisfies BoundPushReq)
    .packetName(SpotActorTransferNames.packetBoundPush)
    .timeout(15000)
    .submit<BoundPushRes>();
  const notify = await pushed;
  require(reply.nodeRid === expectedNode && notify.payload.nodeRid === expectedNode, `${scenario} bound push node mismatch.`);
}

async function assertHttpBoundPush(
  connector: ZlinkStreamConnector,
  node: HttpClient,
  actorId: string,
  scenario: string,
  marker: string,
  expectedNode: string
): Promise<void> {
  const pushed = connector.waitFor<BoundPushNotify>(SpotActorTransferNames.packetBoundNotify)
    .where((message) => message.payload.scenario === scenario && message.payload.marker === marker)
    .timeout(15000).submit();
  const reply = await post<BoundPushRes>(node, `/actors/${actorId}/bound-push`, { scenario, marker } satisfies BoundPushReq);
  const notify = await pushed;
  require(reply.nodeRid === expectedNode && notify.payload.nodeRid === expectedNode, `${scenario} bound push node mismatch.`);
}

async function createSpot(node: HttpClient, spotRid: string, mode = 'accept'): Promise<CreateSpotRes> {
  const created = await post<CreateSpotRes>(node, '/spots', { spotRid, mode } satisfies CreateSpotReq);
  // The Redis-backed public resolver observes a new Spot on its polling turn.
  // Waiting here keeps the scenario on the supported routed transfer path.
  await delay(500);
  return created;
}

async function createActor(node: HttpClient, actorId: string, actorType: string, stateVersion: number): Promise<ActorCreateRes> {
  return await post(node, '/actors', { actorId, actorType, stateVersion } satisfies ActorCreateReq);
}

async function joinActor(node: HttpClient, actorId: string, request: JoinTargetReq): Promise<JoinTargetRes> {
  return await post(node, `/actors/${actorId}/join`, {
    ...request,
    transferId: request.transferId ?? uniqueShort('transfer')
  } satisfies JoinTargetReq);
}

async function probeActor(node: HttpClient, actorId: string, scenario: string, marker: string): Promise<ProbeRes> {
  return await post(node, `/actors/${actorId}/probe`, { scenario, marker } satisfies ProbeReq);
}

async function sendHandoff(node: HttpClient, actorId: string, scenario: string, marker: string): Promise<void> {
  await post(node, `/actors/${actorId}/handoff`, { scenario, marker } satisfies ProbeReq);
}

async function getRef(node: HttpClient, actorId: string): Promise<ActorRefSnapshotRes> {
  return await node.get(`/actors/${actorId}/ref`).fetch<ActorRefSnapshotRes>();
}

async function waitSpotRef(node: HttpClient, spotRid: string, expectedNodeRid: string): Promise<void> {
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    const spot = await node.get(`/spots/${spotRid}/ref`).fetch<{ found: boolean }>();
    if (spot.found) return;
    await delay(100);
  }
  throw new Error(`Spot '${spotRid}' did not resolve while waiting for '${expectedNodeRid}'.`);
}

async function getEvidence(node: HttpClient): Promise<readonly ActorEvidence[]> {
  return await node.get('/evidence').fetch<ActorEvidence[]>();
}

async function waitEvidence(node: HttpClient, containsAll: readonly string[], timeoutMilliseconds = 15000): Promise<readonly ActorEvidence[]> {
  const entries = await post<ActorEvidence[]>(node, '/evidence/wait', {
    containsAll,
    timeoutMilliseconds
  } satisfies EvidenceWaitReq);
  for (const expected of containsAll) {
    require(entries.some((entry) => text(entry).includes(expected)), `Evidence missing '${expected}'.`);
  }
  return entries;
}

async function post<T = unknown>(node: HttpClient, path: string, body: unknown): Promise<T> {
  return await node.post(path).body(body).fetch<T>();
}

function assertOrder(entries: readonly ActorEvidence[], actorId: string, kinds: readonly string[]): void {
  const filtered = entries.filter((entry) => entry.actorId === actorId);
  let cursor = -1;
  for (const kind of kinds) {
    const next = filtered.findIndex((entry, index) => index > cursor && entry.kind === kind);
    require(next > cursor, `Expected '${kind}' after evidence index ${cursor}.`);
    cursor = next;
  }
}

function mergeEvidence(...groups: readonly (readonly ActorEvidence[])[]): readonly ActorEvidence[] {
  return groups.flat().sort((left, right) => {
    const byTime = BigInt(left.atNs) - BigInt(right.atNs);
    if (byTime < 0n) return -1;
    if (byTime > 0n) return 1;
    return left.sequence - right.sequence;
  });
}

function has(entries: readonly ActorEvidence[], actorId: string, kind: string): boolean {
  return entries.some((entry) => entry.actorId === actorId && entry.kind === kind);
}

function text(entry: ActorEvidence): string {
  return `${entry.scenario}|${entry.actorId}|${entry.kind}|${entry.value}|${entry.nodeRid}|transfer=${entry.transferId ?? '<none>'}`;
}

function unique(prefix: string): string { return `${prefix}-${crypto.randomUUID().replaceAll('-', '')}`; }
function uniqueShort(prefix: string): string { return `${prefix}-${crypto.randomUUID().slice(0, 8)}`; }
function delay(ms: number): Promise<void> { return new Promise((resolve) => setTimeout(resolve, ms)); }
async function isPending(promise: Promise<unknown>): Promise<boolean> {
  return await Promise.race([promise.then(() => false, () => false), delay(1).then(() => true)]);
}
function require(condition: unknown, message: string): asserts condition {
  if (!condition) throw new Error(message);
}

function parseOptions(args: readonly string[]): ClientOptions {
  const values = new Map<string, string>();
  for (let index = 0; index < args.length; index += 2) {
    const value = args[index + 1];
    if (value === undefined) throw new Error(`Missing value for '${args[index]}'.`);
    values.set(args[index].replace(/^--/, ''), value);
  }
  const get = (key: string): string => {
    const value = values.get(key);
    if (value === undefined) throw new Error(`--${key} is required.`);
    return value;
  };
  return {
    nodeAUrl: get('node-a-url'),
    nodeBUrl: get('node-b-url'),
    sessionAStreamEndpoint: get('session-a-stream-endpoint'),
    sessionBStreamEndpoint: get('session-b-stream-endpoint'),
    scenario: values.get('scenario') ?? 'all'
  };
}

void runBrowserE2e('SpotActorTransfer', main);

export {};
