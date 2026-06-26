export interface ZLinkSendCall {
  packetName(packetName: string): this;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkRequestCall {
  packetName(packetName: string): this;
  timeout(timeoutMs: number): this;
  submit<TReply>(signal?: AbortSignal): Promise<TReply>;
  yield<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkPublishCall {
  packetName(packetName: string): this;
  submit(signal?: AbortSignal): Promise<void>;
}
