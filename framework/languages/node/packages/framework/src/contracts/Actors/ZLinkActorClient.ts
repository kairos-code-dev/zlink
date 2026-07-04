export interface ZLinkActorClient {
  sendToActor(actorId: string, message: unknown): ZLinkActorSendCall;
  requestToActor(actorId: string, request: unknown): ZLinkActorRequestCall;
}

export interface ZLinkActorSendCall {
  packetName(packetName: string): this;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkActorRequestCall {
  packetName(packetName: string): this;
  timeout(timeoutMs: number): this;
  submit<TReply>(signal?: AbortSignal): Promise<TReply>;
}
