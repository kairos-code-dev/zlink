import type { ActorRef } from '../Common';

export interface ZLinkActorClient {
  sendToActor(actor: ActorRef, message: unknown): ZLinkActorSendCall;
  requestToActor(actor: ActorRef, request: unknown): ZLinkActorRequestCall;
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
