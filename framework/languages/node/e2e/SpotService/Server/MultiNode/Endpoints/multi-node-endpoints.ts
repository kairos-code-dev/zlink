import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  type ZLinkActorClient,
  type ZLinkActorManager,
  type ZLinkLocationRuntimeQuery,
  type ZLinkSpotManager,
  type ZLinkSpotOutbound,
  type ZLinkSpotHandleResolver
} from '@zlink-systems/framework';
import type {
  CreateSpotReq,
  CreateSpotRes,
  EvidenceWaitReq,
  MultiNodeCreateSpotReq,
  MultiNodeStateRouteReq,
  ScaleOutReadinessReq,
  ScaleOutReadinessRes,
  ScaleOutActorProbeRes,
  SpotOnlyJoinRes,
  SpotOnlyMeshReq,
  SpotOnlyMeshRes
} from '../../../Shared/messages';
import { ScaleOutActorProbeReq, SpotOnlyJoinReq, SpotServiceNames, spotServicePacket } from '../../../Shared/messages';
import type { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';
import { createLocalMultiNodeSpot, MultiNodeScenarioActor, requestStateViaSpotOutbound, SpotOnlyUserSpot } from '../Spots/multi-node-spots';

export function createMultiNodeEndpoints(
  evidence: EvidenceStore,
  spots: ZLinkSpotManager,
  outbound: ZLinkSpotOutbound,
  spotRefs: ZLinkSpotHandleResolver,
  actors: ZLinkActorManager,
  actorClient: ZLinkActorClient,
  locations: ZLinkLocationRuntimeQuery,
  actorMeshName: string,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'multi-node', rid: evidence.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/scale-out/readiness/wait',
      handle: (body) => waitForScaleOutReadiness(
        locations, spotRefs, body as ScaleOutReadinessReq
      )
    },
    {
      method: 'POST',
      path: '/evidence/wait',
      handle: (body) => {
        const request = body as EvidenceWaitReq;
        const timeout = Math.max(1, Math.min(request.timeoutMilliseconds ?? 10000, 30000));
        return evidence.waitUntil((entries) =>
          request.containsAll.every((expected) => entries.some((entry) => entry.includes(expected))), timeout);
      }
    },
    {
      method: 'POST',
      path: '/spot/create-local',
      handle: (body) => {
        const request = body as MultiNodeCreateSpotReq;
        return createLocalMultiNodeSpot(spots, evidence, evidence.rid, request.spotRid);
      }
    },
    {
      method: 'POST',
      path: '/spot/create-user-local',
      handle: async (body) => {
        const request = body as CreateSpotReq;
        const created = await spots.getOrCreate(
          SpotServiceNames.spotOnlyMesh,
          SpotOnlyUserSpot,
          request.spotRid
        );
        evidence.add(`create-user-spot|rid=${evidence.rid}|spot=${created.spotRid}|state=${created.state}`);
        return {
          spotRid: String(created.spotRid),
          nodeRid: evidence.rid,
          state: String(created.state)
        } satisfies CreateSpotRes;
      }
    },
    {
      method: 'POST',
      path: '/spot/spot-only/request-send',
      handle: async (body) => {
        const request = body as SpotOnlyMeshReq;
        await spots.getOrCreate(
          SpotServiceNames.spotOnlyMesh,
          SpotOnlyUserSpot,
          request.sourceSpotRid,
          request
        );
        const snapshot = await evidence.waitUntil((entries) =>
          entries.some((entry) =>
            entry.includes(`spot-only-request|rid=${evidence.rid}|source=${request.sourceSpotRid}|target=${request.targetSpotRid}`)
            && entry.includes(`|marker=${request.marker}`)), 10000);
        return {
          sourceSpotRid: request.sourceSpotRid,
          targetSpotRid: request.targetSpotRid,
          targetValue: extractSpotOnlyValue(snapshot, request),
          marker: request.marker
        } satisfies SpotOnlyMeshRes;
      }
    },
    {
      method: 'POST',
      path: '/actor/spot-only-join',
      handle: async (body) => {
        const bodyRequest = body as SpotOnlyJoinReq;
        const request = new SpotOnlyJoinReq(
          bodyRequest.targetSpotRid,
          bodyRequest.actorId,
          bodyRequest.marker
        );
        const actor = await actors.getOrCreate(
          actorMeshName,
          request.actorId,
          SpotServiceNames.actorType,
          { displayName: `spot-only-${request.actorId}` }
        );
        const result = await actorClient
          .requestToActor(actorMeshName, actor, request)
          .timeout(10000)
          .submit<SpotOnlyJoinRes>();
        await evidence.waitUntil((entries) =>
          entries.some((entry) =>
            entry.includes(`spot-only-actor-join|rid=${evidence.rid}|actor=${request.actorId}|target=${request.targetSpotRid}`)
            && entry.includes(`|marker=${request.marker}`)), 10000);
        return result;
      }
    },
    {
      method: 'POST',
      path: '/actor/scale-out-probe',
      handle: async (body) => {
        const request = body as ScaleOutActorProbeReq;
        const actor = await actors.getOrCreate(
          actorMeshName,
          request.actorId,
          SpotServiceNames.actorType,
          { displayName: `scale-out-${request.actorId}` }
        );
        return await actorClient
          .requestToActor(actorMeshName, actor, spotServicePacket(ScaleOutActorProbeReq, request))
          .timeout(10_000)
          .submit<ScaleOutActorProbeRes>();
      }
    },
    {
      method: 'POST',
      path: '/spot/state/request',
      handle: (body) => {
        const request = body as MultiNodeStateRouteReq;
        return requestStateViaSpotOutbound(
          outbound,
          spotRefs,
          actorMeshName,
          request.spotRid,
          request.delta
        );
      }
    },
    {
      method: 'POST',
      path: '/shutdown',
      handle: () => {
        stop();
        return { status: 'stopping' };
      }
    },
    {
      method: 'POST',
      path: '/crash',
      handle: () => {
        setTimeout(() => process.exit(1), 10);
        return { status: 'crashing' };
      }
    }
  ];
}

async function waitForScaleOutReadiness(
  locations: ZLinkLocationRuntimeQuery,
  spotRefs: ZLinkSpotHandleResolver,
  request: ScaleOutReadinessReq
): Promise<ScaleOutReadinessRes> {
  const deadline = Date.now() + Math.max(1, Math.min(request.timeoutMilliseconds ?? 30_000, 30_000));
  do {
    const rows = await locations.listPeerLocations({
      autoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
      meshName: SpotServiceNames.spotOnlyMesh,
      role: ZLinkLocationRole.Spot
    });
    const peer = rows.find((row) => String(row.nodeRid) === request.nodeRid);
    const capabilities = peer?.capabilities ?? [];
    const entrySpotReady = await spotRefs.resolveSpotHandle(
      SpotServiceNames.spotOnlyMesh,
      request.nodeRid
    ) !== undefined;
    if (peer !== undefined
      && capabilities.includes(`actor:${SpotServiceNames.actorType}`)
      && entrySpotReady) {
      return {
        nodeRid: request.nodeRid,
        peerReady: true,
        entrySpotReady: true,
        capabilities
      };
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  } while (Date.now() < deadline);
  throw new Error(`SpotNode '${request.nodeRid}' did not publish peer, actor capability, and Entry Spot readiness.`);
}

function extractSpotOnlyValue(evidence: readonly string[], request: SpotOnlyMeshReq): number {
  const entry = [...evidence].reverse().find((line) =>
    line.includes(`source=${request.sourceSpotRid}`)
    && line.includes(`target=${request.targetSpotRid}`)
    && line.includes(`marker=${request.marker}`));
  const match = entry?.match(/\|value=(\d+)/);
  return match == null ? 0 : Number(match[1]);
}
