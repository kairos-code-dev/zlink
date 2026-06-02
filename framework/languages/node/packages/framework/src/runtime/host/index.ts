import { ZLinkNodeBackendAdapterFactory } from '../backend';
import type { ZLinkBackendAdapterFactory, ZLinkBackendContext } from '../backend';
import type { ZLinkFrameworkRegistration } from '../configuration';

export interface ZLinkFrameworkRuntime {
  readonly isStarted: boolean;
  start(): Promise<void>;
  stop(): Promise<void>;
}

export interface ZLinkFrameworkRuntimeHostOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly backendAdapterFactory?: ZLinkBackendAdapterFactory;
  readonly lifecycleSink?: string[];
}

export class ZLinkFrameworkRuntimeHost implements ZLinkFrameworkRuntime {
  private readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  private readonly lifecycleSink?: string[];
  private context?: ZLinkBackendContext;

  constructor(readonly options: ZLinkFrameworkRuntimeHostOptions) {
    this.backendAdapterFactory = options.backendAdapterFactory ?? new ZLinkNodeBackendAdapterFactory();
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
