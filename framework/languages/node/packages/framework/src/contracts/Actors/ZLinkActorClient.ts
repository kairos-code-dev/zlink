import type { ActorRef } from '../Common';

export interface ZLinkActorClient {
  sendToActor(actor: ActorRef, message: unknown): ZLinkActorSendCall;
  requestToActor(actor: ActorRef, request: unknown): ZLinkActorRequestCall;
}

export interface ZLinkActorSendCall {
  metadata(key: string, value: string): this;
  submit(): void;
}

export interface ZLinkActorRequestCall {
  metadata(key: string, value: string): this;
  timeout(timeoutMs: number): this;
  submit<TReply>(signal?: AbortSignal): Promise<TReply>;
  yield<TReply>(signal?: AbortSignal): Promise<TReply>;
}
