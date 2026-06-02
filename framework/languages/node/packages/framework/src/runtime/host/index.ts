export interface ZLinkFrameworkRuntime {
  start(): Promise<void>;
  stop(): Promise<void>;
}
