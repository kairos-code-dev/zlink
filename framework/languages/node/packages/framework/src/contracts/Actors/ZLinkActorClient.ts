import type { ActorRef } from '../Common';

export interface ZLinkActorClient {
  sendToActor(meshName: string, actor: ActorRef, message: unknown): ZLinkActorSendCall;
  requestToActor(meshName: string, actor: ActorRef, request: unknown): ZLinkActorRequestCall;
}

export interface ZLinkActorSendCall {
  metadata(key: string, value: string): this;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkActorRequestCall {
  metadata(key: string, value: string): this;
  timeout(timeoutMs: number): this;
  submit<TReply>(signal?: AbortSignal): Promise<TReply>;
  yield<TReply>(signal?: AbortSignal): Promise<TReply>;
}
