// SM-G2: 두 번째 SpotNode 추가 전후의 기존 owner 유지와 신규 로컬 배치를 검증한다.
import type {
  CreateSpotReq,
  CreateSpotRes,
  EvidenceWaitReq,
  MultiNodeStateRouteReq,
  ScaleOutActorProbeReq,
  ScaleOutActorProbeRes,
  ScaleOutReadinessReq,
  ScaleOutReadinessRes,
  SpotOnlyJoinReq,
  SpotOnlyJoinRes,
  StateRes
} from '../../Shared/messages';
import { SpotServiceNames } from '../../Shared/messages';
import type { ClientOptions } from '../Support/client-options';
import { getJson, postJson, postJsonWithin } from '../../../http-client';
import { ensure } from '../Support/scenario-assert';

const existingSpotRid = 'spot-sm-g2-existing';
const existingActorId = 'actor-sm-g2-existing';
const newSpotRid = 'spot-sm-g2-new';
const newActorId = 'actor-sm-g2-new';

export async function prepareSmG2(options: ClientOptions): Promise<void> {
  const created = await createLocalSpot(options.multiAUrl, existingSpotRid);
  const joined = await joinActor(options.multiAUrl, existingSpotRid, existingActorId, 'before-scale-out');
  const state = await requestState(options.multiAUrl, existingSpotRid, 1);

  ensure(created.nodeRid === SpotServiceNames.multiSpotNodeA, 'SM-G2 existing Spot owner was not node A.');
  ensure(joined.accepted, 'SM-G2 existing actor JoinReq was rejected.');
  ensure(state.nodeRid === SpotServiceNames.multiSpotNodeA, 'SM-G2 existing Spot request reached the wrong owner.');
  console.log('scenario SM-G2 prepare passed');
}

export async function verifySmG2(options: ClientOptions): Promise<void> {
  const readiness = await postJsonWithin<ScaleOutReadinessRes>(
    options.multiAUrl,
    '/scale-out/readiness/wait',
    {
      nodeRid: SpotServiceNames.multiSpotNodeB,
      timeoutMilliseconds: 30_000
    } satisfies ScaleOutReadinessReq,
    31_000
  );
  ensure(
    readiness.peerReady
      && readiness.entrySpotReady
      && readiness.capabilities.includes(`actor:${SpotServiceNames.actorType}`),
    'SM-G2 node B peer, actor capability, or Entry Spot handle was not ready.'
  );

  const existingOwner = await requestState(options.multiAUrl, existingSpotRid, 1);
  const existingActor = await probeActor(options.multiAUrl, existingActorId, 'after-scale-out');
  ensure(
    existingOwner.nodeRid === SpotServiceNames.multiSpotNodeA,
    'SM-G2 scale-out moved the existing Spot owner away from node A.'
  );
  ensure(
    existingActor.nodeRid === SpotServiceNames.multiSpotNodeA,
    'SM-G2 scale-out moved the existing actor owner away from node A.'
  );

  const created = await createLocalSpot(options.multiBUrl, newSpotRid);
  const newOwner = await joinActor(options.multiBUrl, newSpotRid, newActorId, 'new-node');
  const newState = await requestState(options.multiAUrl, newSpotRid, 1);
  ensure(created.nodeRid === SpotServiceNames.multiSpotNodeB, 'SM-G2 new Spot was not created locally on node B.');
  ensure(newOwner.accepted, 'SM-G2 selected node B Entry Spot rejected the new actor JoinReq.');
  ensure(newState.nodeRid === SpotServiceNames.multiSpotNodeB, 'SM-G2 new Spot request reached the wrong owner.');

  const [nodeAEvidence, nodeBEvidence] = await Promise.all([
    waitForEvidence(options.multiAUrl, [
      `scale-out-actor-probe|rid=${SpotServiceNames.multiSpotNodeA}|spot=${existingSpotRid}`
        + `|actor=${existingActorId}|marker=after-scale-out`,
      `spot-state-request|rid=${SpotServiceNames.multiSpotNodeA}|spot=${existingSpotRid}|value=2`
    ]),
    waitForEvidence(options.multiBUrl, [
      `entry-created|rid=${SpotServiceNames.multiSpotNodeB}|actor=${newActorId}`,
      `spot-only-actor-join|rid=${SpotServiceNames.multiSpotNodeB}|actor=${newActorId}`,
      `spot-state-request|rid=${SpotServiceNames.multiSpotNodeB}|spot=${newSpotRid}|value=1`
    ])
  ]);
  ensure(
    nodeAEvidence.every((line) => !line.includes(`actor=${newActorId}`))
      && nodeBEvidence.every((line) => !line.includes(`actor=${existingActorId}`)),
    'SM-G2 actor ownership crossed nodes during scale-out.'
  );
  console.log('scenario SM-G2 verify passed');
}

async function createLocalSpot(url: string, spotRid: string): Promise<CreateSpotRes> {
  return await postJson<CreateSpotRes>(url, '/spot/create-user-local', {
    spotRid
  } satisfies CreateSpotReq);
}

async function joinActor(
  url: string,
  targetSpotRid: string,
  actorId: string,
  marker: string
): Promise<SpotOnlyJoinRes> {
  return await postJson<SpotOnlyJoinRes>(url, '/actor/spot-only-join', {
    targetSpotRid,
    actorId,
    marker
  } satisfies SpotOnlyJoinReq);
}

async function requestState(url: string, spotRid: string, delta: number): Promise<StateRes> {
  return await postJson<StateRes>(url, '/spot/state/request', {
    spotRid,
    delta
  } satisfies MultiNodeStateRouteReq);
}

async function probeActor(
  url: string,
  actorId: string,
  marker: string
): Promise<ScaleOutActorProbeRes> {
  return await postJson<ScaleOutActorProbeRes>(url, '/actor/scale-out-probe', {
    actorId,
    marker
  } satisfies ScaleOutActorProbeReq);
}

async function waitForEvidence(url: string, containsAll: readonly string[]): Promise<readonly string[]> {
  await postJsonWithin<readonly string[]>(url, '/evidence/wait', {
    containsAll,
    timeoutMilliseconds: 10_000
  } satisfies EvidenceWaitReq, 11_000);
  return await getJson<readonly string[]>(url, '/evidence');
}
