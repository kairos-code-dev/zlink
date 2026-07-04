export interface ZLinkSendCall {
  packetName(packetName: string): this;
  submit(signal?: AbortSignal): void;
}

export interface ZLinkRequestCall {
  packetName(packetName: string): this;
  timeout(timeoutMs: number): this;
  submit<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkYieldRequestCall extends ZLinkRequestCall {
  packetName(packetName: string): this;
  timeout(timeoutMs: number): this;
  yield<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkPublishCall {
  packetName(packetName: string): this;
  submit(signal?: AbortSignal): void;
}
