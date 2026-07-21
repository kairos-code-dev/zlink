import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  type ZLinkRouteClient,
  type ZLinkSpotManager,
  type ZLinkSpotOutbound,
  type ZLinkSpotHandleResolver
} from '@zlink-systems/framework';
import type {
  ChannelEchoRes,
  ChannelRouteRes,
  ChannelRouteReq,
  CloseSpotReq,
  CrossRoleActorPushRes,
  CreateSpotReq,
  EvidenceWaitReq,
  SpotIdleCloseReq,
  SpotMissingMsgReq,
  SpotMissingHandlerReq,
  SpotMissingTargetMsgReq,
  SpotMissingTargetReq,
  SpotMixedRouteRes,
  SpotMixedRouteReq,
  SpotStageProbeReq,
  SpotStageTimerReq,
  SpotOverrunStartReq,
  SpotOutboundRouteReq,
  SpotPublishReq,
  SpotSlowRouteReq,
  SpotStateMsgReq,
  SpotStateRouteReq,
  SpotTimerStartReq,
  SpotToSpotNegativeRes,
  SpotToSpotNegativeRouteReq,
  SpotToSpotRes,
  SpotToSpotRouteReq,
  SpotToSpotTimeoutRes,
  SpotToSpotTimeoutRouteReq,
  SpotTypeMismatchReq,
  SpotWorkerCompleteReq,
  SpotWorkerStartReq,
  StateRes
} from '../../../Shared/messages';
import {
  ChannelEchoReq,
  CrossRoleActorPushReq,
  MissingSpotMsg,
  MissingSpotReq,
  SlowSpotReq,
  SpotAdminReq,
  SpotOutboundMsg,
  SpotOutboundNegativeMsg,
  SpotServiceNames,
  SpotToSpotNegativeReq,
  SpotToSpotReq,
  SpotToSpotTimeoutReq,
  StageProbeReq,
  StageTimerStartMsg,
  StateMsg,
  StateReq,
  spotServicePacket
} from '../../../Shared/messages';
import type { EvidenceStore } from '../Infrastructure/evidence-store';
import { InMemorySpotRouteStore } from '../Infrastructure/spot-route-store';
import { ScenarioAlternateSpot, ScenarioUserSpot } from '../Spots/scenario-spots';
import type { HttpRoute } from '../Support/http-server';

export function createPlayEndpoints(
  evidence: EvidenceStore,
  spotManager: ZLinkSpotManager,
  spotOutbound: ZLinkSpotOutbound,
  spotRefs: ZLinkSpotHandleResolver,
  routeClient: ZLinkRouteClient,
  stop: () => void
): HttpRoute[] {
  return [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ready', role: 'play', rid: evidence.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'POST',
      path: '/crash',
      handle: () => {
        setTimeout(() => process.exit(1), 10);
        return { status: 'crashing' };
      }
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
      path: '/spot/create',
      handle: async (body) => {
        const request = body as CreateSpotReq;
        const created = await spotManager.getOrCreate(
          SpotServiceNames.spotChannel,
          ScenarioUserSpot,
          request.spotRid
        );
        const state = typeof created.state === 'string' ? created.state : String(created.state);
        InMemorySpotRouteStore.recordUserSpot(String(created.spotRid), evidence.rid);
        evidence.add(`create-spot|rid=${evidence.rid}|spot=${created.spotRid}|state=${state}`);
        return { spotRid: String(created.spotRid), nodeRid: evidence.rid, state };
      }
    },
    {
      method: 'POST',
      path: '/spot/close',
      handle: async (body) => {
        const request = body as CloseSpotReq;
        const closed = await spotManager.close(SpotServiceNames.spotChannel, request.spotRid);
        evidence.add(`close-spot|rid=${evidence.rid}|spot=${request.spotRid}|closed=${closed}`);
        if (closed) {
          await evidence.waitUntil((entries) =>
            entries.some((entry) => entry.includes(`spot-closing|rid=${evidence.rid}|spot=${request.spotRid}`)), 10000);
        }
        return { spotRid: request.spotRid, closed };
      }
    },
    {
      method: 'POST',
      path: '/spot/type-mismatch',
      handle: async (body) => {
        const request = body as SpotTypeMismatchReq;
        const first = await spotManager.getOrCreate(
          SpotServiceNames.spotChannel,
          ScenarioUserSpot,
          request.spotRid
        );
        try {
          await spotManager.getOrCreate(
            SpotServiceNames.spotChannel,
            ScenarioAlternateSpot,
            request.spotRid
          );
        } catch (error) {
          if (error instanceof ZLinkFrameworkException && error.kind === ZLinkFrameworkErrorKind.SpotTypeMismatch) {
            evidence.add(`spot-type-mismatch|rid=${evidence.rid}|spot=${request.spotRid}|kind=SpotTypeMismatch`);
            const state = typeof first.state === 'string' ? first.state : String(first.state);
            return {
              spotRid: request.spotRid,
              failed: true,
              errorKind: 'SpotTypeMismatch',
              state
            };
          }
          throw error;
        }
        throw new Error('Expected SpotTypeMismatch for reused spot rid.');
      }
    },
    {
      method: 'POST',
      path: '/spot/create-alternate',
      handle: async (body) => {
        const request = body as CreateSpotReq;
        const created = await spotManager.getOrCreate(
          SpotServiceNames.spotChannel,
          ScenarioAlternateSpot,
          request.spotRid
        );
        const state = typeof created.state === 'string' ? created.state : String(created.state);
        InMemorySpotRouteStore.recordUserSpot(String(created.spotRid), evidence.rid);
        evidence.add(`create-alternate-spot|rid=${evidence.rid}|spot=${created.spotRid}|state=${state}`);
        return { spotRid: String(created.spotRid), nodeRid: evidence.rid, state };
      }
    },
    {
      method: 'POST',
      path: '/spot/state/request',
      handle: (body) => requestSpotState(spotOutbound, spotRefs, body as SpotStateRouteReq)
    },
    {
      method: 'POST',
      path: '/spot/stage/request',
      handle: async (body) => {
        const request = body as SpotStageProbeReq;
        const spot = await requireSpotRef(spotRefs, request.spotRid);
        return await spotOutbound
          .requestToSpot(spot, spotServicePacket(StageProbeReq,
            { marker: request.marker, delta: request.delta }))
          .timeout(5000)
          .submit<StateRes>();
      }
    },
    {
      method: 'POST',
      path: '/spot/stage/timer',
      handle: async (body) => {
        const request = body as SpotStageTimerReq;
        const before = evidence.snapshot();
        const spot = await requireSpotRef(spotRefs, request.spotRid);
        await spotOutbound
          .sendToSpot(spot, spotServicePacket(StageTimerStartMsg,
            { name: request.name, periodMs: request.periodMs }))
          .submit();
        const marker = `stage-timer|rid=${evidence.rid}|spot=${request.spotRid}|name=${request.name}`;
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, marker) >= 1, 10000);
        return {
          spotRid: request.spotRid,
          name: request.name,
          started: true,
          evidence: snapshot
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/state/command',
      handle: async (body) => {
        const request = body as SpotStateMsgReq;
        const before = evidence.snapshot();
        const spot = await requireSpotRef(spotRefs, request.spotRid);
        await spotOutbound
          .sendToSpot(spot, spotServicePacket(StateMsg, { marker: request.marker }))
          .submit();
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, `spot-state-command|rid=${evidence.rid}|spot=${request.spotRid}|marker=${request.marker}`) >= 1,
          10000);
        return {
          spotRid: request.spotRid,
          marker: request.marker,
          accepted: true,
          evidence: snapshot
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/slow/request',
      handle: async (body) => {
        const request = body as SpotSlowRouteReq;
        const timedOut = await fails(async () => {
          const spot = await requireSpotRef(spotRefs, request.spotRid);
          await spotOutbound
            .requestToSpot(spot, spotServicePacket(SlowSpotReq,
              { marker: request.marker, delayMs: request.delayMs }))
            .timeout(request.timeoutMs)
            .submit();
        });
        return {
          spotRid: request.spotRid,
          marker: request.marker,
          timedOut
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/outbound',
      handle: async (body) => {
        const request = body as SpotOutboundRouteReq;
        const before = evidence.snapshot();
        const spot = await requireSpotRef(spotRefs, request.spotRid);
        await spotOutbound
          .sendToSpot(spot, spotServicePacket(SpotOutboundMsg, { marker: request.marker }))
          .submit();
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, `spot-outbound|rid=${evidence.rid}|spot=${request.spotRid}|echo=echo-sm-c2|notify=notify-sm-c2`) >= 1
          && countNew(entries, before, `spot-msg|rid=${evidence.rid}|spot=${request.spotRid}|marker=sm-c2-publish`) >= 1
          && countNew(entries, before, 'channel-echo|value=sm-c2') >= 1
          && countNew(entries, before, 'channel-notify|marker=notify-sm-c2') >= 1,
          10000);
        return {
          spotRid: request.spotRid,
          marker: request.marker,
          accepted: true,
          evidence: snapshot
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/outbound-negative',
      handle: async (body) => {
        const request = body as SpotOutboundRouteReq;
        const before = evidence.snapshot();
        const spot = await requireSpotRef(spotRefs, request.spotRid);
        await spotOutbound
          .sendToSpot(spot, spotServicePacket(SpotOutboundNegativeMsg, { marker: request.marker }))
          .submit();
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, `spot-outbound-negative|rid=${evidence.rid}|spot=${request.spotRid}|requestFailed=True`) >= 1
          && countNew(entries, before, 'dispatch-error|surface=channel|kind=request|reason=no_handler|action=reply_error|packet=MissingChannelReq') >= 1
          && countNew(entries, before, 'dispatch-error|surface=channel|kind=send|reason=no_handler|action=drop|packet=MissingChannelNotify') >= 1,
          10000);
        return {
          spotRid: request.spotRid,
          marker: request.marker,
          accepted: true,
          evidence: snapshot
        };
      }
    },
    {
      method: 'POST',
      path: '/channel/route/request',
      handle: async (body) => {
        const request = body as ChannelRouteReq;
        const channel = await routeClient
          .requestToNode(SpotServiceNames.externalSpotChannel, request.targetNodeRid,
            spotServicePacket(ChannelEchoReq, { value: request.value }))
          .timeout(5000)
          .submit<ChannelEchoRes>();
        return { value: channel.value } satisfies ChannelRouteRes;
      }
    },
    {
      method: 'POST',
      path: '/actor/cross-role/push',
      handle: async (body) => {
        const request = body as CrossRoleActorPushReq;
        const reply = await routeClient
          .requestToNode(SpotServiceNames.externalSpotChannel, request.nodeRid,
            spotServicePacket(CrossRoleActorPushReq, request))
          .timeout(5000)
          .submit<CrossRoleActorPushRes>();
        evidence.add(
          `cross-role-entry|rid=${evidence.rid}|target=${request.nodeRid}|actor=${request.actorId}|value=${request.value}`
        );
        return reply;
      }
    },
    {
      method: 'POST',
      path: '/spot/mixed-route/request',
      handle: async (body) => {
        const request = body as SpotMixedRouteReq;
        InMemorySpotRouteStore.recordUserSpot(request.spotRid, request.targetNodeRid);
        const channel = await routeClient
          .requestToNode(SpotServiceNames.externalSpotChannel, request.targetNodeRid,
            spotServicePacket(ChannelEchoReq, { value: request.channelValue }))
          .timeout(5000)
          .submit<ChannelEchoRes>();
        const spot = await requireSpotRef(spotRefs, request.spotRid);
        const state = await spotOutbound
          .requestToSpot(spot, spotServicePacket(StateReq, { operation: 'add', delta: request.delta }))
          .timeout(5000)
          .submit<StateRes>();
        return {
          spotRid: request.spotRid,
          channelReply: channel.value,
          spotValue: state.value
        } satisfies SpotMixedRouteRes;
      }
    },
    {
      method: 'POST',
      path: '/spot/to-spot/request',
      handle: async (body) => {
        const request = body as SpotToSpotRouteReq;
        const sourceSpot = await requireSpotRef(spotRefs, request.sourceSpotRid);
        const targetSpot = await requireSpotRef(spotRefs, request.targetSpotRid);
        return await spotOutbound
          .requestToSpot(sourceSpot, spotServicePacket(SpotToSpotReq, {
            targetSpotRid: request.targetSpotRid,
            targetSpot,
            marker: request.marker
          }))
          .timeout(5000)
          .submit<SpotToSpotRes>();
      }
    },
    {
      method: 'POST',
      path: '/spot/to-spot/timeout',
      handle: async (body) => {
        const request = body as SpotToSpotTimeoutRouteReq;
        const sourceSpot = await requireSpotRef(spotRefs, request.sourceSpotRid);
        const targetSpot = await requireSpotRef(spotRefs, request.targetSpotRid);
        return spotOutbound
          .requestToSpot(sourceSpot, spotServicePacket(SpotToSpotTimeoutReq, {
            targetSpotRid: request.targetSpotRid,
            targetSpot,
            marker: request.marker
          }))
          .timeout(5000)
          .submit<SpotToSpotTimeoutRes>();
      }
    },
    {
      method: 'POST',
      path: '/spot/to-spot/negative',
      handle: async (body) => {
        const request = body as SpotToSpotNegativeRouteReq;
        const sourceSpot = await requireSpotRef(spotRefs, request.sourceSpotRid);
        const targetSpot = await requireSpotRef(spotRefs, request.targetSpotRid);
        return spotOutbound
          .requestToSpot(sourceSpot, spotServicePacket(SpotToSpotNegativeReq, {
            targetSpotRid: request.targetSpotRid,
            targetSpot,
            marker: request.marker
          }))
          .timeout(5000)
          .submit<SpotToSpotNegativeRes>();
      }
    },
    {
      method: 'POST',
      path: '/spot/publish/wait',
      handle: async (body) => {
        const request = body as SpotPublishReq;
        const snapshot = await evidence.waitUntil((entries) =>
          entries.some((entry) =>
            entry.includes(`spot-msg|rid=${evidence.rid}|spot=${request.spotRid}|marker=${request.marker}`)),
          30000);
        return {
          operation: 'spot.sm-c4-observe',
          spotRid: request.spotRid,
          marker: request.marker,
          received: true,
          evidence: snapshot
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/publish/local',
      handle: async (body) => {
        const request = body as SpotPublishReq;
        await submitSpotAdmin(spotOutbound, spotRefs, request.spotRid, new SpotAdminReq('publish', request.marker));
        return {
          operation: 'spot.sm-c5-publish',
          publisherRid: evidence.rid,
          spotRid: request.spotRid,
          marker: request.marker,
          evidence: evidence.snapshot()
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/missing-handler/request',
      handle: async (body) => {
        const request = body as SpotMissingHandlerReq;
        const before = evidence.snapshot();
        const failed = await fails(async () => {
          const spot = await requireSpotRef(spotRefs, request.spotRid);
          await spotOutbound
            .requestToSpot(spot, spotServicePacket(MissingSpotReq, { operation: 'noop', delta: 0 }))
            .timeout(2000)
            .submit<StateRes>();
        });
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, 'dispatch-error|surface=spot|kind=request|reason=no_handler|action=fail_caller|packet=MissingSpotReq') >= 1,
          10000);
        return {
          spotRid: request.spotRid,
          failed,
          evidence: snapshot
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/missing-handler/command',
      handle: async (body) => {
        const request = body as SpotMissingMsgReq;
        const before = evidence.snapshot();
        const abort = new AbortController();
        const timeout = setTimeout(() => abort.abort(), 2000);
        try {
          const spot = await requireSpotRef(spotRefs, request.spotRid);
          await spotOutbound
            .sendToSpot(spot, spotServicePacket(MissingSpotMsg, { marker: request.marker }))
            .submit();
        } catch (error) {
          if (!abort.signal.aborted) {
            throw error;
          }
        } finally {
          clearTimeout(timeout);
        }
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, 'dispatch-error|surface=spot|kind=send|reason=no_handler|action=drop|packet=MissingSpotMsg') >= 1,
          10000);
        return {
          spotRid: request.spotRid,
          marker: request.marker,
          sent: true,
          evidence: snapshot
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/missing-target/request',
      handle: async (body) => {
        const request = body as SpotMissingTargetReq;
        const failed = await fails(async () => {
          const spot = await requireSpotRef(spotRefs, request.spotRid);
          await spotOutbound
            .requestToSpot(spot, spotServicePacket(StateReq, { operation: 'noop', delta: 0 }))
            .timeout(2000)
            .submit<StateRes>();
        });
        return {
          spotRid: request.spotRid,
          failed,
          evidence: evidence.snapshot()
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/missing-target/command',
      handle: async (body) => {
        const request = body as SpotMissingTargetMsgReq;
        InMemorySpotRouteStore.recordUserSpot(request.spotRid, evidence.rid);
        const before = evidence.snapshot();
        const spot = await requireSpotRef(spotRefs, request.spotRid);
        await spotOutbound
          .sendToSpot(spot, spotServicePacket(StateMsg, { marker: request.marker }))
          .submit();
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, 'dispatch-error|surface=spot|kind=send|reason=no_handler|action=drop|packet=StateMsg') >= 1,
          10000);
        return {
          spotRid: request.spotRid,
          marker: request.marker,
          sent: true,
          evidence: snapshot
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/state/local',
      handle: async (body) => {
        const request = body as SpotStateRouteReq;
        return requestSpotState(spotOutbound, spotRefs, request);
      }
    },
    {
      method: 'POST',
      path: '/spot/worker/start',
      handle: (body) => {
        const request = body as SpotWorkerStartReq;
        evidence.add(`worker-start|rid=${evidence.rid}|spot=${request.spotRid}|marker=${request.marker}`);
        void submitSpotAdmin(spotOutbound, spotRefs, request.spotRid,
          new SpotAdminReq('worker', request.marker, undefined, undefined, request.delayMs))
          .catch((error: unknown) => evidence.add(
            `worker-error|rid=${evidence.rid}|spot=${request.spotRid}|marker=${request.marker}|error=${String(error)}`
          ));
        return {
          spotRid: request.spotRid,
          nodeRid: evidence.rid,
          marker: request.marker
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/worker/complete',
      handle: async (body) => {
        const request = body as SpotWorkerCompleteReq;
        const marker = `worker-complete|rid=${evidence.rid}|spot=${request.spotRid}|marker=${request.marker}`;
        const snapshot = await evidence.waitUntil((entries) =>
          entries.some((entry) => entry.includes(marker)), 30000);
        return {
          spotRid: request.spotRid,
          marker: request.marker,
          completed: true,
          evidence: snapshot
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/idle-close/start',
      handle: async (body) => {
        const request = body as SpotIdleCloseReq;
        const before = evidence.snapshot();
        await submitSpotAdmin(spotOutbound, spotRefs, request.spotRid,
          new SpotAdminReq('idleTimer', undefined, request.name, request.periodMs));
        const idleMarker = `timer-idle-close|rid=${evidence.rid}|spot=${request.spotRid}|name=${request.name}|closed=True`;
        const closingMarker = `spot-closing|rid=${evidence.rid}|spot=${request.spotRid}`;
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, idleMarker) === 1
          && countNew(entries, before, closingMarker) === 1,
          30000);
        return {
          spotRid: request.spotRid,
          name: request.name,
          closed: true,
          evidence: snapshot
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/timer/start',
      handle: async (body) => {
        const request = body as SpotTimerStartReq;
        const before = evidence.snapshot();
        await submitSpotAdmin(spotOutbound, spotRefs, request.spotRid,
          new SpotAdminReq('timer', undefined, request.name, request.periodMs));
        const marker = `timer-basic|rid=${evidence.rid}|spot=${request.spotRid}|name=${request.name}`;
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, marker) >= 2, 30000);
        return {
          spotRid: request.spotRid,
          name: request.name,
          started: true,
          evidence: snapshot
        };
      }
    },
    {
      method: 'POST',
      path: '/spot/overrun/start',
      handle: async (body) => {
        const request = body as SpotOverrunStartReq;
        const before = evidence.snapshot();
        await submitSpotAdmin(spotOutbound, spotRefs, request.spotRid,
          new SpotAdminReq('overrunTimer', undefined, request.name, request.periodMs, undefined,
            request.policy as SpotAdminReq['policy']));
        const marker = `timer-overrun|rid=${evidence.rid}|spot=${request.spotRid}|name=${request.name}`;
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, marker) >= 3, 30000);
        return {
          spotRid: request.spotRid,
          name: request.name,
          policy: request.policy,
          started: true,
          evidence: snapshot
        };
      }
    },
    { method: 'POST', path: '/shutdown', handle: () => { stop(); return { status: 'stopping' }; } }
  ];
}

function countNew(entries: readonly string[], before: readonly string[], marker: string): number {
  return entries.slice(before.length).filter((entry) => entry.includes(marker)).length;
}

async function requestSpotState(
  spotOutbound: ZLinkSpotOutbound,
  spotRefs: ZLinkSpotHandleResolver,
  request: SpotStateRouteReq
): Promise<StateRes> {
  const spot = await requireSpotRef(spotRefs, request.spotRid);
  return await spotOutbound
    .requestToSpot(spot, spotServicePacket(StateReq,
      { operation: request.operation, delta: request.delta }))
    .timeout(5000)
    .submit<StateRes>();
}

async function submitSpotAdmin(
  spotOutbound: ZLinkSpotOutbound,
  spotRefs: ZLinkSpotHandleResolver,
  spotRid: string,
  request: SpotAdminReq
): Promise<unknown> {
  const spot = await requireSpotRef(spotRefs, spotRid);
  return spotOutbound.requestToSpot(spot, request).timeout(30000).submit();
}

async function requireSpotRef(spotRefs: ZLinkSpotHandleResolver, spotRid: string) {
  const spot = await spotRefs.resolveSpotHandle(SpotServiceNames.spotChannel, spotRid);
  if (spot === undefined) {
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.SpotRouteNotFound,
      `SPOT '${spotRid}' has no live location row.`
    );
  }
  return spot;
}

async function fails(operation: () => Promise<void>): Promise<boolean> {
  try {
    await operation();
    return false;
  } catch {
    return true;
  }
}
