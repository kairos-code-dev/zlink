export interface ZLinkBoundSession {
  send(message: unknown): ZLinkBoundSessionSendCall;
  disconnect(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkBoundSessionFactory {
  create(actorId: string): ZLinkBoundSession;
}

export interface ZLinkBoundSessionSendCall {
  metadata(key: string, value: string): this;
  packetName(packetName: string): this;
  submit(signal?: AbortSignal): void;
}
