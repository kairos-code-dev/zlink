import type { ZLinkBackendContext } from '../backend';
import type { ZLinkChannelRuntimeManager } from '../channels';
import type { ZLinkFrameworkRuntimeState } from '../execution';
import type { ZLinkLocationRuntime } from '../locations';
import type { ZLinkSpotNodeRuntimeManager } from '../spots';
import type { ZLinkStreamRuntimeManager } from '../streams';
import type { ZLinkLocationRuntimeStopSnapshot } from './location-runtime-owner';
import type { ZLinkMonitoringRuntime } from './monitoring-runtime';

export interface ZLinkRuntimeStartRollbackParts {
  readonly context: ZLinkBackendContext;
  readonly startedLocationRuntime?: ZLinkLocationRuntime;
  readonly monitoringRuntime?: ZLinkMonitoringRuntime;
  readonly streamRuntime?: ZLinkStreamRuntimeManager;
  readonly spotNodeRuntime?: ZLinkSpotNodeRuntimeManager;
  readonly channelRuntime?: ZLinkChannelRuntimeManager;
}

export interface ZLinkRuntimeStopParts {
  readonly state: ZLinkFrameworkRuntimeState;
  readonly locationSnapshot: ZLinkLocationRuntimeStopSnapshot;
  readonly monitoringRuntime?: ZLinkMonitoringRuntime;
  readonly streamRuntime?: ZLinkStreamRuntimeManager;
  readonly spotNodeRuntime?: ZLinkSpotNodeRuntimeManager;
  readonly channelRuntime?: ZLinkChannelRuntimeManager;
}

export async function rollbackRuntimeStart(parts: ZLinkRuntimeStartRollbackParts): Promise<void> {
  await Promise.allSettled([
    parts.startedLocationRuntime?.stop(),
    parts.monitoringRuntime?.dispose(),
    parts.streamRuntime?.dispose(),
    parts.spotNodeRuntime?.dispose(),
    parts.channelRuntime?.dispose(),
    parts.context.dispose()
  ]);
}

export async function stopRuntimeParts(parts: ZLinkRuntimeStopParts): Promise<void> {
  const state = parts.state;
  state.abortController.abort();
  await parts.monitoringRuntime?.dispose();
  await parts.streamRuntime?.dispose();
  await parts.spotNodeRuntime?.dispose(state.abortController.signal);
  await parts.channelRuntime?.dispose(state.abortController.signal);
  parts.locationSnapshot.lifecycle?.dispose();
  await parts.locationSnapshot.runtime?.stop();
  await Promise.allSettled(state.listenerTasks);
  (state.context as unknown as { shutdown(): void }).shutdown();
  await nextRuntimeShutdownTurn();
  await state.dispose();
  await nextRuntimeShutdownTurn();
}

function nextRuntimeShutdownTurn(): Promise<void> {
  return new Promise((resolve) => setImmediate(resolve));
}
