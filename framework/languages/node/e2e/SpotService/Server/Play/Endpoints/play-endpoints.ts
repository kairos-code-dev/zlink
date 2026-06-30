import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  type ZLinkRouteClient,
  type ZLinkSpotManager,
  type ZLinkSpotOutbound
} from '@zlink-systems/framework';
import type {
  ChannelEchoReply,
  ChannelRouteReply,
  ChannelRouteReq,
  CloseSpotReq,
  CreateSpotReq,
  EvidenceWaitRequest,
  SpotIdleCloseReq,
  SpotMissingCommandReq,
  SpotMissingHandlerReq,
  SpotMissingTargetCommandReq,
  SpotMissingTargetReq,
  SpotMixedRouteReply,
  SpotMixedRouteReq,
  SpotStageProbeReq,
  SpotStageTimerReq,
  SpotOverrunStartReq,
  SpotOutboundRouteReq,
  SpotPublishReq,
  SpotSlowRouteReq,
  SpotStateCommandReq,
  SpotStateRouteReq,
  SpotTimerStartReq,
  SpotToSpotNegativeReply,
  SpotToSpotNegativeRouteReq,
  SpotToSpotReply,
  SpotToSpotRouteReq,
  SpotToSpotTimeoutReply,
  SpotToSpotTimeoutRouteReq,
  SpotTypeMismatchReq,
  SpotWorkerCompleteReq,
  SpotWorkerStartReq,
  StateReply
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
        const request = body as EvidenceWaitRequest;
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
      handle: (body) => requestSpotStateWithRetry(spotOutbound, body as SpotStateRouteReq)
    },
    {
      method: 'POST',
      path: '/spot/stage/request',
      handle: (body) => {
        const request = body as SpotStageProbeReq;
        return retryUntil(async () => spotOutbound
          .requestToSpot(request.spotRid, { marker: request.marker, delta: request.delta })
          .packetName('StageProbeReq')
          .timeout(5000)
          .submit<StateReply>(), 'spot stage route request');
      }
    },
    {
      method: 'POST',
      path: '/spot/stage/timer',
      handle: async (body) => {
        const request = body as SpotStageTimerReq;
        const before = evidence.snapshot();
        await retryUntil(async () => spotOutbound
          .sendToSpot(request.spotRid, { name: request.name, periodMs: request.periodMs })
          .packetName('StageTimerStartCommand')
          .submit(), 'spot stage timer command route');
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
        const request = body as SpotStateCommandReq;
        const before = evidence.snapshot();
        await spotOutbound
          .sendToSpot(request.spotRid, { marker: request.marker })
          .packetName('StateCommand')
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
          await spotOutbound
            .requestToSpot(request.spotRid, { marker: request.marker, delayMs: request.delayMs })
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
        await spotOutbound
          .sendToSpot(request.spotRid, { marker: request.marker })
          .packetName('SpotOutboundReq')
          .submit();
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, `spot-outbound|rid=${evidence.rid}|spot=${request.spotRid}|echo=echo-sm-c2|notify=notify-sm-c2`) >= 1
          && countNew(entries, before, `spot-event|rid=${evidence.rid}|spot=${request.spotRid}|marker=sm-c2-publish`) >= 1
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
        await spotOutbound
          .sendToSpot(request.spotRid, { marker: request.marker })
          .packetName('SpotOutboundNegativeReq')
          .submit();
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, `spot-outbound-negative|rid=${evidence.rid}|spot=${request.spotRid}|requestFailed=True`) >= 1
          && countNew(entries, before, 'dispatch-error|surface=channel|kind=request|reason=handlerMissing|action=replyError|packet=MissingChannelReq') >= 1
          && countNew(entries, before, 'dispatch-error|surface=channel|kind=send|reason=handlerMissing|action=drop|packet=MissingChannelSend') >= 1,
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
          .request(SpotServiceNames.externalSpotChannel, request.targetNodeRid, { value: request.value })
          .packetName('ChannelEchoReq')
          .timeout(5000)
          .submit<ChannelEchoReply>();
        return { value: channel.value } satisfies ChannelRouteReply;
      }
    },
    {
      method: 'POST',
      path: '/spot/mixed-route/request',
      handle: async (body) => {
        const request = body as SpotMixedRouteReq;
        InMemorySpotRouteStore.recordUserSpot(request.spotRid, request.targetNodeRid);
        const channel = await routeClient
          .request(SpotServiceNames.externalSpotChannel, request.targetNodeRid, { value: request.channelValue })
          .packetName('ChannelEchoReq')
          .timeout(5000)
          .submit<ChannelEchoReply>();
        const state = await retryUntil(async () => spotOutbound
          .requestToSpot(request.spotRid, { operation: 'add', delta: request.delta })
          .packetName('StateReq')
          .timeout(5000)
          .submit<StateReply>(), 'mixed route spot request');
        return {
          spotRid: request.spotRid,
          channelReply: channel.value,
          spotValue: state.value
        } satisfies SpotMixedRouteReply;
      }
    },
    {
      method: 'POST',
      path: '/spot/to-spot/request',
      handle: (body) => {
        const request = body as SpotToSpotRouteReq;
        return spotOutbound
          .requestToSpot(request.sourceSpotRid, {
            targetSpotRid: request.targetSpotRid,
            marker: request.marker
          })
          .packetName('SpotToSpotReq')
          .timeout(5000)
          .submit<SpotToSpotReply>();
      }
    },
    {
      method: 'POST',
      path: '/spot/to-spot/timeout',
      handle: (body) => {
        const request = body as SpotToSpotTimeoutRouteReq;
        return spotOutbound
          .requestToSpot(request.sourceSpotRid, {
            targetSpotRid: request.targetSpotRid,
            marker: request.marker
          })
          .packetName('SpotToSpotTimeoutReq')
          .timeout(5000)
          .submit<SpotToSpotTimeoutReply>();
      }
    },
    {
      method: 'POST',
      path: '/spot/to-spot/negative',
      handle: (body) => {
        const request = body as SpotToSpotNegativeRouteReq;
        return spotOutbound
          .requestToSpot(request.sourceSpotRid, {
            targetSpotRid: request.targetSpotRid,
            marker: request.marker
          })
          .packetName('SpotToSpotNegativeReq')
          .timeout(5000)
          .submit<SpotToSpotNegativeReply>();
      }
    },
    {
      method: 'POST',
      path: '/spot/publish/wait',
      handle: async (body) => {
        const request = body as SpotPublishReq;
        const snapshot = await evidence.waitUntil((entries) =>
          entries.some((entry) =>
            entry.includes(`spot-event|rid=${evidence.rid}|spot=${request.spotRid}|marker=${request.marker}`)),
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
      path: '/spot/missing-handler/request',
      handle: async (body) => {
        const request = body as SpotMissingHandlerReq;
        const before = evidence.snapshot();
        const failed = await fails(async () => {
          await spotOutbound
            .requestToSpot(request.spotRid, { operation: 'noop', delta: 0 })
            .packetName('MissingSpotReq')
            .timeout(2000)
            .submit<StateReply>();
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
        const request = body as SpotMissingCommandReq;
        const before = evidence.snapshot();
        const abort = new AbortController();
        const timeout = setTimeout(() => abort.abort(), 2000);
        try {
          await spotOutbound
            .sendToSpot(request.spotRid, { marker: request.marker })
            .packetName('MissingSpotCommand')
            .submit(abort.signal);
        } catch (error) {
          if (!abort.signal.aborted) {
            throw error;
          }
        } finally {
          clearTimeout(timeout);
        }
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, 'dispatch-error|surface=spotRoute|kind=send|reason=handlerMissing|action=drop|packet=MissingSpotCommand') >= 1,
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
          await spotOutbound
            .requestToSpot(request.spotRid, { operation: 'noop', delta: 0 })
            .packetName('StateReq')
            .timeout(2000)
            .submit<StateReply>();
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
        const request = body as SpotMissingTargetCommandReq;
        InMemorySpotRouteStore.recordUserSpot(request.spotRid, evidence.rid);
        const before = evidence.snapshot();
        await spotOutbound
          .sendToSpot(request.spotRid, { marker: request.marker })
          .packetName('StateCommand')
          .submit();
        const snapshot = await evidence.waitUntil((entries) =>
          countNew(entries, before, 'dispatch-error|surface=spotRoute|kind=send|reason=handlerMissing|action=drop|packet=StateCommand') >= 1,
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
        return spotManager.executeOnSpot(ScenarioUserSpot, request.spotRid, (spot): StateReply => {
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

async function requestSpotStateWithRetry(
  spotOutbound: ZLinkSpotOutbound,
  request: SpotStateRouteReq
): Promise<StateReply> {
  return retryUntil(async () => spotOutbound
    .requestToSpot(request.spotRid, { operation: request.operation, delta: request.delta })
    .packetName('StateReq')
    .timeout(5000)
    .submit<StateReply>(), 'spot state route request');
}

async function retryUntil<T>(operation: () => Promise<T>, label: string): Promise<T> {
  const deadline = Date.now() + 30000;
  let last: unknown;
  while (Date.now() < deadline) {
    try {
      return await operation();
    } catch (error) {
      last = error;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
  }
  throw new Error(`Timed out waiting for ${label}: ${last instanceof Error ? last.message : String(last)}`);
}

async function fails(operation: () => Promise<void>): Promise<boolean> {
  try {
    await operation();
    return false;
  } catch {
    return true;
  }
}
