import { ZLinkNodeBackendAdapterFactory } from '../backend';
import type { ZLinkBackendAdapterFactory, ZLinkBackendContext } from '../backend';
import type { ZLinkFrameworkRegistration } from '../configuration';
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
  private context?: ZLinkBackendContext;
  readonly streamBindingRuntime = new ZLinkStreamBindingRuntime();
  readonly boundSessionFactory = new DefaultZLinkBoundSessionFactory(this.streamBindingRuntime);

  constructor(readonly options: ZLinkFrameworkRuntimeHostOptions, internalOptions?: unknown) {
    this.backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
    this.lifecycleSink = options.lifecycleSink;
  }

  get isStarted(): boolean {
    return this.context !== undefined;
  }

  async start(): Promise<void> {
    if (this.context !== undefined) {
      return;
    }

    this.lifecycleSink?.push('framework:start');
    const channelAdapter = this.backendAdapterFactory.createChannelAdapter();
    const context = channelAdapter.createContext();
    try {
      this.context = context;
      this.lifecycleSink?.push('framework:started');
    } catch (error) {
      await context.dispose();
      throw error;
    }
  }

  async stop(): Promise<void> {
    const context = this.context;
    if (context === undefined) {
      return;
    }

    this.context = undefined;
    this.lifecycleSink?.push('framework:stop');
    await context.dispose();
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
