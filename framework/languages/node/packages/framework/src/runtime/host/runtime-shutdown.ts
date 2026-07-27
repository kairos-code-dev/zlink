import type { ZLinkBackendContext } from '../backend';
import type { ZLinkChannelRuntimeManager } from '../channels';
import type { ZLinkFrameworkExecutionState } from '../execution';
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
  readonly state: ZLinkFrameworkExecutionState;
  readonly locationSnapshot: ZLinkLocationRuntimeStopSnapshot;
  readonly monitoringRuntime?: ZLinkMonitoringRuntime;
  readonly streamRuntime?: ZLinkStreamRuntimeManager;
  readonly spotNodeRuntime?: ZLinkSpotNodeRuntimeManager;
  readonly channelRuntime?: ZLinkChannelRuntimeManager;
  readonly serviceRelocation?: { dispose(): Promise<void> };
}

export async function rollbackRuntimeStart(parts: ZLinkRuntimeStartRollbackParts): Promise<void> {
  await Promise.allSettled([
    parts.monitoringRuntime?.dispose(),
    parts.streamRuntime?.dispose(),
    parts.spotNodeRuntime?.dispose(),
    parts.channelRuntime?.dispose()
  ]);
  await parts.startedLocationRuntime?.stop().catch(() => undefined);
  await parts.context.dispose().catch(() => undefined);
}

export async function stopRuntimeParts(parts: ZLinkRuntimeStopParts): Promise<void> {
  const state = parts.state;
  state.abortController.abort();
  const errors: unknown[] = [];
  await runShutdownStep(errors, () => parts.monitoringRuntime?.dispose());
  await runShutdownStep(errors, () => parts.streamRuntime?.dispose());
  await runShutdownStep(errors, () => parts.spotNodeRuntime?.dispose());
  await runShutdownStep(errors, () => parts.channelRuntime?.dispose());
  await runShutdownStep(errors, () => parts.serviceRelocation?.dispose());
  await runShutdownStep(errors, () => parts.locationSnapshot.lifecycle?.dispose());
  await runShutdownStep(errors, () => parts.locationSnapshot.runtime?.stop());
  await Promise.allSettled(state.listenerTasks);
  await runShutdownStep(errors, () => state.dispose());
  const failures = errors.filter((error) => !isAbortError(error));
  if (failures.length === 1) throw failures[0];
  if (failures.length > 1) {
    throw new AggregateError(failures, 'Framework runtime shutdown failed.');
  }
}

async function runShutdownStep(
  errors: unknown[],
  step: () => Promise<unknown> | unknown
): Promise<void> {
  try {
    await step();
  } catch (error) {
    errors.push(error);
  }
}

function isAbortError(error: unknown): boolean {
  if (error instanceof Error && error.name === 'AbortError') return true;
  return error instanceof AggregateError
    && error.errors.every((nested) => isAbortError(nested));
}
