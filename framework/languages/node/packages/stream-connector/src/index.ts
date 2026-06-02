export interface ZlinkStreamConnectorOptions {
  readonly endpoint: string;
}

export interface ZlinkStreamConnector {
  connect(): Promise<void>;
  close(): Promise<void>;
}
