import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  type ZLinkRouteClient,
  type ZLinkSpotManager,
  type ZLinkSpotOutbound,
  type ZLinkSpotRefResolver
} from '@zlink-systems/framework';
import type {
  ChannelEchoRes,
  ChannelRouteRes,
  ChannelRouteReq,
  CloseSpotReq,
  CrossRoleActorPushReq,
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
import { SpotServiceNames } from '../../../Shared/messages';
import { ZLinkTimerOverrunPolicy } from '@zlink-systems/framework';
import { BasicTimerHandler, IdleCloseTimerHandler, OverrunTimerHandler } from '../Handlers/timer-handlers';
import type { EvidenceStore } from '../Infrastructure/evidence-store';
import { InMemorySpotRouteStore } from '../Infrastructure/spot-route-store';
import { ScenarioAlternateSpot, ScenarioUserSpot } from '../Spots/scenario-spots';
import type { HttpRoute } from '../Support/http-server';

export function createPlayEndpoints(
  evidence: EvidenceStore,
  spotManager: ZLinkSpotManager,
  spotOutbound: ZLinkSpotOutbound,
  spotRefs: ZLinkSpotRefResolver,
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
        const created = await spotManager.getOrCreate(ScenarioUserSpot, request.spotRid);
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
        const closed = await spotManager.close(request.spotRid);
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
        const first = await spotManager.getOrCreate(ScenarioUserSpot, request.spotRid);
        try {
          await spotManager.getOrCreate(ScenarioAlternateSpot, request.spotRid);
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
        const created = await spotManager.getOrCreate(ScenarioAlternateSpot, request.spotRid);
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
          .requestToSpot(spot, { marker: request.marker, delta: request.delta })
          .packetName('StageProbeReq')
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
          .sendToSpot(spot, { name: request.name, periodMs: request.periodMs })
          .packetName('StageTimerStartMsg')
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
          .sendToSpot(spot, { marker: request.marker })
          .packetName('StateMsg')
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
            .requestToSpot(spot, { marker: request.marker, delayMs: request.delayMs })
            .packetName('SlowSpotReq')
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
          .sendToSpot(spot, { marker: request.marker })
          .packetName('SpotOutboundMsg')
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
          .sendToSpot(spot, { marker: request.marker })
          .packetName('SpotOutboundNegativeMsg')
          .submit();
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, `spot-outbound-negative|rid=${evidence.rid}|spot=${request.spotRid}|requestFailed=True`) >= 1
          && countNew(entries, before, 'dispatch-error|surface=channel|kind=request|reason=handlerMissing|action=replyError|packet=MissingChannelReq') >= 1
          && countNew(entries, before, 'dispatch-error|surface=channel|kind=send|reason=handlerMissing|action=drop|packet=MissingChannelNotify') >= 1,
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
          .requestToNode(SpotServiceNames.externalSpotChannel, request.targetNodeRid, { value: request.value })
          .packetName('ChannelEchoReq')
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
          .requestToNode(SpotServiceNames.externalSpotChannel, request.nodeRid, request)
          .packetName('CrossRoleActorPushReq')
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
          .requestToNode(SpotServiceNames.externalSpotChannel, request.targetNodeRid, { value: request.channelValue })
          .packetName('ChannelEchoReq')
          .timeout(5000)
          .submit<ChannelEchoRes>();
        const spot = await requireSpotRef(spotRefs, request.spotRid);
        const state = await spotOutbound
          .requestToSpot(spot, { operation: 'add', delta: request.delta })
          .packetName('StateReq')
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
          .requestToSpot(sourceSpot, {
            targetSpotRid: request.targetSpotRid,
            targetSpot,
            marker: request.marker
          })
          .packetName('SpotToSpotReq')
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
          .requestToSpot(sourceSpot, {
            targetSpotRid: request.targetSpotRid,
            targetSpot,
            marker: request.marker
          })
          .packetName('SpotToSpotTimeoutReq')
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
          .requestToSpot(sourceSpot, {
            targetSpotRid: request.targetSpotRid,
            targetSpot,
            marker: request.marker
          })
          .packetName('SpotToSpotNegativeReq')
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
        await spotManager.executeOnSpot(ScenarioUserSpot, request.spotRid, async (spot) => {
          await spot.context.outbound
            .publish(SpotServiceNames.spotEventTopic, { marker: request.marker })
            .packetName('SpotMsg')
            .submit();
          evidence.add(`spot-publish|rid=${evidence.rid}|spot=${request.spotRid}|marker=${request.marker}`);
        });
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
            .requestToSpot(spot, { operation: 'noop', delta: 0 })
            .packetName('MissingSpotReq')
            .timeout(2000)
            .submit<StateRes>();
        });
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, 'dispatch-error|surface=spotRoute|kind=request|reason=handlerMissing|action=replyError|packet=MissingSpotReq') >= 1,
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
            .sendToSpot(spot, { marker: request.marker })
            .packetName('MissingSpotMsg')
            .submit(abort.signal);
        } catch (error) {
          if (!abort.signal.aborted) {
            throw error;
          }
        } finally {
          clearTimeout(timeout);
        }
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, 'dispatch-error|surface=spotRoute|kind=send|reason=handlerMissing|action=drop|packet=MissingSpotMsg') >= 1,
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
        InMemorySpotRouteStore.recordUserSpot(request.spotRid, evidence.rid);
        const before = evidence.snapshot();
        const failed = await fails(async () => {
          const spot = staleLocalSpotRef(evidence.rid, request.spotRid);
          await spotOutbound
            .requestToSpot(spot, { operation: 'noop', delta: 0 })
            .packetName('StateReq')
            .timeout(2000)
            .submit<StateRes>();
        });
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, 'dispatch-error|surface=spotRoute|kind=request|reason=handlerMissing|action=replyError|packet=StateReq') >= 1,
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
      path: '/spot/missing-target/command',
      handle: async (body) => {
        const request = body as SpotMissingTargetMsgReq;
        InMemorySpotRouteStore.recordUserSpot(request.spotRid, evidence.rid);
        const before = evidence.snapshot();
        const spot = staleLocalSpotRef(evidence.rid, request.spotRid);
        await spotOutbound
          .sendToSpot(spot, { marker: request.marker })
          .packetName('StateMsg')
          .submit();
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, 'dispatch-error|surface=spotRoute|kind=send|reason=handlerMissing|action=drop|packet=StateMsg') >= 1,
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
      handle: (body) => {
        const request = body as SpotStateRouteReq;
        return spotManager.executeOnSpot(ScenarioUserSpot, request.spotRid, (spot): StateRes => {
          const delta = request.operation === 'add' ? request.delta : 0;
          const value = spot.add(delta);
          evidence.add(`spot-state-request|rid=${evidence.rid}|spot=${spot.context.spotRid}|value=${value}`);
          return {
            spotRid: String(spot.context.spotRid),
            nodeRid: String(spot.context.nodeRid),
            value
          };
        });
      }
    },
    {
      method: 'POST',
      path: '/spot/worker/start',
      handle: (body) => {
        const request = body as SpotWorkerStartReq;
        return spotManager.executeOnSpot(ScenarioUserSpot, request.spotRid, (spot) => {
          evidence.add(`worker-start|rid=${evidence.rid}|spot=${spot.context.spotRid}|marker=${request.marker}`);
          spot.context.runWorker((signal) => delayWorker(request.marker, request.delayMs, signal))
            .onCompleted((marker, signal) => {
              signal?.throwIfAborted();
              spot.add(100);
              evidence.add(`worker-complete|rid=${evidence.rid}|spot=${spot.context.spotRid}|marker=${marker}`);
            });
          return {
            spotRid: String(spot.context.spotRid),
            nodeRid: String(spot.context.nodeRid),
            marker: request.marker
          };
        });
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
        await spotManager.executeOnSpot(ScenarioUserSpot, request.spotRid, async (spot) => {
          await spot.context.addTimer(request.name, request.periodMs, IdleCloseTimerHandler);
        });
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
        await spotManager.executeOnSpot(ScenarioUserSpot, request.spotRid, async (spot) => {
          await spot.context.addTimer(request.name, request.periodMs, BasicTimerHandler);
        });
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
        await spotManager.executeOnSpot(ScenarioUserSpot, request.spotRid, async (spot) => {
          await spot.context.addTimer(
            request.name,
            request.periodMs,
            OverrunTimerHandler,
            {
              overrunPolicy: parseOverrunPolicy(request.policy),
              maxCatchUpTicks: 2
            }
          );
        });
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

function parseOverrunPolicy(policy: string): ZLinkTimerOverrunPolicy {
  switch (policy) {
    case 'SkipLateTicks':
      return ZLinkTimerOverrunPolicy.SkipLateTicks;
    case 'CatchUpBounded':
      return ZLinkTimerOverrunPolicy.CatchUpBounded;
    case 'DelayNextTick':
      return ZLinkTimerOverrunPolicy.DelayNextTick;
    default:
      throw new Error(`Unsupported overrun policy '${policy}'.`);
  }
}

function countNew(entries: readonly string[], before: readonly string[], marker: string): number {
  return entries.slice(before.length).filter((entry) => entry.includes(marker)).length;
}

function delayWorker(marker: string, delayMs: number, signal?: AbortSignal): Promise<string> {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) {
      reject(signal.reason);
      return;
    }
    const timer = setTimeout(() => {
      signal?.removeEventListener('abort', onAbort);
      resolve(marker);
    }, delayMs);
    const onAbort = (): void => {
      clearTimeout(timer);
      reject(signal?.reason ?? new Error('Worker aborted.'));
    };
    signal?.addEventListener('abort', onAbort, { once: true });
  });
}

function staleLocalSpotRef(nodeRid: string, spotRid: string) {
  return {
    meshName: SpotServiceNames.spotChannel,
    nodeRid,
    spotRid
  };
}

async function requestSpotState(
  spotOutbound: ZLinkSpotOutbound,
  spotRefs: ZLinkSpotRefResolver,
  request: SpotStateRouteReq
): Promise<StateRes> {
  const spot = await requireSpotRef(spotRefs, request.spotRid);
  return await spotOutbound
    .requestToSpot(spot, { operation: request.operation, delta: request.delta })
    .packetName('StateReq')
    .timeout(5000)
    .submit<StateRes>();
}

async function requireSpotRef(spotRefs: ZLinkSpotRefResolver, spotRid: string) {
  const spot = await spotRefs.resolveSpotRef(spotRid);
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
