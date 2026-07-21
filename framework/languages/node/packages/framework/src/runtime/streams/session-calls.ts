import type {
  ZLinkBoundSessionSendCall,
  ZLinkSessionReplyCall,
  ZLinkSessionSendCall,
  ZLinkSubmitResult
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { throwIfAborted } from '../abort';
import {
  ensureSingleSubmit,
  resolvePacketName,
  type ZLinkStreamFrameHeader,
  ZLinkStreamMessageKind,
  type ZLinkStreamReplyMessageKind
} from './protocol';

export interface ZLinkBoundSessionSendRuntime {
  sendBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<ZLinkSubmitResult>;
}

export interface ZLinkSessionCallContext {
  readonly stream: {
    writeRaw(payload: Message): boolean;
    submitRaw(payload: Message, signal?: AbortSignal): Promise<ZLinkSubmitResult>;
  };
  readonly dispatchHeader: ZLinkStreamFrameHeader | undefined;
  createJsonFrameMessage(
    kind: ZLinkStreamMessageKind,
    packetName: string,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    requestSeq: bigint | undefined,
    payload: unknown,
    correlationId?: string
  ): Message;
  createJsonReplyFrameMessage(
    requestHeader: ZLinkStreamFrameHeader,
    kind: ZLinkStreamReplyMessageKind,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    payload: unknown
  ): Message;
  claimReply(requestHeader: ZLinkStreamFrameHeader): void;
}

export class DefaultZLinkBoundSessionSendCall implements ZLinkBoundSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private executed = false;

  constructor(
    private readonly runtime: ZLinkBoundSessionSendRuntime,
    private readonly actorId: string,
    private readonly message: unknown
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  packetName(packetName: string): this {
    this.selectedPacketName = packetName;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<ZLinkSubmitResult> {
    ensureSingleSubmit(this.executed);
    const packetName = resolvePacketName(this.message, this.selectedPacketName);
    this.executed = true;
    throwIfAborted(signal);
    return await this.runtime.sendBoundSession(
      this.actorId,
      this.message,
      packetName,
      this.selectedMetadata,
      signal
    );
  }
}

export class DefaultZLinkSessionSendCall implements ZLinkSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private compressionEnabled = false;
  private executed = false;

  constructor(
    private readonly context: ZLinkSessionCallContext,
    private readonly message: unknown
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  packetName(packetName: string): this {
    this.selectedPacketName = packetName;
    return this;
  }

  compress(enabled = true): this {
    this.compressionEnabled = enabled;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<ZLinkSubmitResult> {
    ensureSingleSubmit(this.executed);
    const packetName = resolvePacketName(this.message, this.selectedPacketName);
    this.executed = true;
    throwIfAborted(signal);
    const message = this.context.createJsonFrameMessage(
      ZLinkStreamMessageKind.Send,
      packetName,
      this.selectedMetadata,
      this.compressionEnabled,
      undefined,
      this.message
    );
    try {
      return await this.context.stream.submitRaw(message, signal);
    } finally {
      message.close();
    }
  }
}

export class DefaultZLinkSessionReplyCall implements ZLinkSessionReplyCall {
  private compressionEnabled = false;
  private executed = false;

  constructor(
    private readonly context: ZLinkSessionCallContext,
    private readonly message: unknown
  ) {}

  compress(enabled = true): this {
    this.compressionEnabled = enabled;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<ZLinkSubmitResult> {
    ensureSingleSubmit(this.executed);
    this.executed = true;
    const requestHeader = this.context.dispatchHeader;
    if (requestHeader?.requestSeq === undefined) {
      throw new Error('Reply is only available while handling a request packet.');
    }
    this.context.claimReply(requestHeader);
    // Reply-token validity wins over caller cancellation. Claiming before the
    // pre-cancel check keeps duplicate calls deterministic while still making
    // a pre-cancelled first call perform no transport admission.
    throwIfAborted(signal);
    const message = this.context.createJsonReplyFrameMessage(
      requestHeader,
      ZLinkStreamMessageKind.Response,
      new Map<string, string>(),
      this.compressionEnabled,
      this.message
    );
    try {
      return await this.context.stream.submitRaw(message, signal);
    } finally {
      message.close();
    }
  }
}
