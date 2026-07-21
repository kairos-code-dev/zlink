import type { ZLinkRuntimeEventPublisher } from '../../contracts';
import type { ZLinkBackendMeshNode, ZLinkBackendSocketMonitor, ZLinkMonitoringBackendAdapter } from '../backend';
import type { ZLinkChannelRuntimeManager } from '../channels';
import type { ZLinkFrameworkRegistration } from '../configuration';
import {
  ZLinkLocationRuntimeMonitoringSource,
  ZLinkMeshMonitoringSource,
  ZLinkSocketMonitoringSource,
} from '../diagnostics';
import type { ZLinkFrameworkRuntimeState } from '../execution';
import type { ZLinkLocationRuntime } from '../locations';

export interface ZLinkMonitoringRuntimeOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly channelRuntime: ZLinkChannelRuntimeManager;
  readonly meshNodes: ReadonlyMap<string, ZLinkBackendMeshNode>;
  readonly locationRuntime?: ZLinkLocationRuntime;
  readonly monitoringAdapter: ZLinkMonitoringBackendAdapter;
  readonly publisher: ZLinkRuntimeEventPublisher;
}

export class ZLinkMonitoringRuntime {
  private readonly monitors: ZLinkBackendSocketMonitor[] = [];

  constructor(private readonly options: ZLinkMonitoringRuntimeOptions) {}

  start(state: ZLinkFrameworkRuntimeState): void {
    const monitoring = this.options.registration.monitoring;
    if (monitoring === undefined) {
      return;
    }
    for (const registration of monitoring.socket ?? []) {
      const monitor = this.options.channelRuntime.openMonitoringSource(registration.sourceName, this.options.monitoringAdapter);
      this.monitors.push(monitor);
      new ZLinkSocketMonitoringSource(registration, monitor, this.options.publisher).start();
    }
    for (const registration of monitoring.locationRuntime ?? []) {
      const query = this.options.locationRuntime;
      if (query === undefined) {
        throw new Error(`Monitoring location runtime source '${registration.sourceName}' requires location stores.`);
      }
      const source = new ZLinkLocationRuntimeMonitoringSource(registration, query, this.options.publisher);
      state.listenerTasks.push(state.taskRunner.run(
        `monitoring:location-runtime:${registration.sourceName}`,
        (signal) => runPollingMonitoringSource(registration.intervalMs, signal, () => source.pollOnce(signal))
      ));
    }
    for (const registration of monitoring.spot ?? []) {
      const meshNode = this.options.meshNodes.get(registration.sourceName);
      if (meshNode === undefined) {
        throw new Error(`Monitoring spot source '${registration.sourceName}' is not registered.`);
      }
      const source = new ZLinkMeshMonitoringSource(
        registration,
        meshNode,
        this.options.publisher,
        this.options.registration.spotNodes.get(registration.sourceName)
      );
      state.listenerTasks.push(state.taskRunner.run(
        `monitoring:spot:${registration.sourceName}`,
        (signal) => runPollingMonitoringSource(registration.intervalMs, signal, () => source.pollOnce())
      ));
    }
  }

  async dispose(): Promise<void> {
    const monitors = [...this.monitors];
    this.monitors.length = 0;
    await Promise.allSettled(monitors.reverse().map((monitor) => monitor.dispose()));
  }
}

async function runPollingMonitoringSource(
  intervalMs: number,
  signal: AbortSignal,
  pollOnce: () => Promise<void>
): Promise<void> {
  while (!signal.aborted) {
    await pollOnce();
    await delayMonitoringPoll(intervalMs, signal);
  }
}

function delayMonitoringPoll(intervalMs: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve) => {
    const timeout = setTimeout(resolve, intervalMs);
    signal.addEventListener('abort', () => {
      clearTimeout(timeout);
      resolve();
    }, { once: true });
  });
}
