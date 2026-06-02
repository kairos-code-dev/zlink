import { ZLinkNodeBackendAdapterFactory } from '../backend';
import type { ZLinkBackendAdapterFactory, ZLinkBackendContext } from '../backend';
import type { ZLinkFrameworkRegistration } from '../configuration';
import { ZLinkFrameworkRuntimeState } from '../execution';
import { DefaultZLinkBoundSessionFactory, ZLinkStreamBindingRuntime } from '../streams';

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
    try {
      this.state = new ZLinkFrameworkRuntimeState(context);
      this.lifecycleSink?.push('framework:started');
    } catch (error) {
      await context.dispose();
      throw error;
    }
  }

  async stop(): Promise<void> {
    const state = this.state;
    if (state === undefined) {
      return;
    }

    this.state = undefined;
    this.lifecycleSink?.push('framework:stop');
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
