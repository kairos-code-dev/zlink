import { ZLinkNodeBackendAdapterFactory } from '../backend';
import type { ZLinkBackendAdapterFactory, ZLinkBackendContext } from '../backend';
import type { ZLinkFrameworkRegistration } from '../configuration';
import {
  ZLinkChannelRuntimeManager,
  ZLinkRuntimeChannelTransport,
  ZLinkRuntimeRouteTransport
} from '../channels';
import { ZLinkFrameworkRuntimeState } from '../execution';
import {
  DefaultZLinkBoundSessionFactory,
  ZLinkStreamBindingRuntime,
  ZLinkStreamRuntimeManager
} from '../streams';

export interface ZLinkFrameworkRuntime {
  readonly isStarted: boolean;
  start(): Promise<void>;
  stop(): Promise<void>;
}

export interface ZLinkFrameworkRuntimeHostOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly lifecycleSink?: string[];
}

export class ZLinkFrameworkRuntimeHost implements ZLinkFrameworkRuntime {
  private readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  private readonly lifecycleSink?: string[];
  private state?: ZLinkFrameworkRuntimeState;
  private channelRuntime?: ZLinkChannelRuntimeManager;
  private streamRuntime?: ZLinkStreamRuntimeManager;
  readonly channelTransport = new ZLinkRuntimeChannelTransport(() => this.channelRuntime);
  readonly routeTransport = new ZLinkRuntimeRouteTransport(() => this.channelRuntime);
  readonly streamBindingRuntime = new ZLinkStreamBindingRuntime();
  readonly boundSessionFactory = new DefaultZLinkBoundSessionFactory(this.streamBindingRuntime);

  constructor(readonly options: ZLinkFrameworkRuntimeHostOptions, internalOptions?: unknown) {
    this.backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
    this.lifecycleSink = options.lifecycleSink;
  }

  get isStarted(): boolean {
    return this.state !== undefined;
  }

  get context(): ZLinkBackendContext | undefined {
    return this.state?.context as ZLinkBackendContext | undefined;
  }

  get taskRunner(): ZLinkFrameworkRuntimeState['taskRunner'] | undefined {
    return this.state?.taskRunner;
  }

  get errorSink(): ZLinkFrameworkRuntimeState['errorSink'] | undefined {
    return this.state?.errorSink;
  }

  async start(): Promise<void> {
    if (this.state !== undefined) {
      return;
    }

    this.lifecycleSink?.push('framework:start');
    const channelAdapter = this.backendAdapterFactory.createChannelAdapter();
    const context = channelAdapter.createContext();
    let channelRuntime: ZLinkChannelRuntimeManager | undefined;
    let streamRuntime: ZLinkStreamRuntimeManager | undefined;
    try {
      this.state = new ZLinkFrameworkRuntimeState(context);
      channelRuntime = new ZLinkChannelRuntimeManager(this.options.registration, channelAdapter, context);
      this.state.listenerTasks.push(...channelRuntime.start(this.state.taskRunner));
      this.channelRuntime = channelRuntime;
      streamRuntime = new ZLinkStreamRuntimeManager({
        registration: this.options.registration,
        backendAdapterFactory: this.backendAdapterFactory,
        context,
        bindingRuntime: this.streamBindingRuntime
      });
      streamRuntime.start();
      this.streamRuntime = streamRuntime;
      this.lifecycleSink?.push('framework:started');
    } catch (error) {
      await streamRuntime?.dispose();
      await channelRuntime?.dispose();
      await context.dispose();
      throw error;
    }
  }

  async stop(): Promise<void> {
    const state = this.state;
    if (state === undefined) {
      return;
    }

    const channelRuntime = this.channelRuntime;
    const streamRuntime = this.streamRuntime;
    this.state = undefined;
    this.channelRuntime = undefined;
    this.streamRuntime = undefined;
    this.lifecycleSink?.push('framework:stop');
    await streamRuntime?.dispose();
    await channelRuntime?.dispose();
    await state.dispose();
    this.lifecycleSink?.push('framework:stopped');
  }

  async onApplicationBootstrap(): Promise<void> {
    await this.start();
  }

  async onApplicationShutdown(): Promise<void> {
    await this.stop();
  }
}

function resolveBackendAdapterFactory(internalOptions: unknown): ZLinkBackendAdapterFactory {
  if (
    typeof internalOptions === 'object'
    && internalOptions !== null
    && 'backendAdapterFactory' in internalOptions
  ) {
    const factory = (internalOptions as { readonly backendAdapterFactory?: ZLinkBackendAdapterFactory }).backendAdapterFactory;
    if (factory !== undefined) {
      return factory;
    }
  }
  return new ZLinkNodeBackendAdapterFactory();
}
