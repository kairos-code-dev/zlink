export const RegistrationCodecNames = {
  channel: 'reg-codec'
} as const;

export const PacketNames = {
  echoAuto: 'EchoAuto',
  echoAutoCommand: 'EchoAutoCommand',
  echoAttr: 'EchoAttr',
  echoAttrCommand: 'EchoAttrCommand',
  echoManual: 'EchoManual',
  echoManualCommand: 'EchoManualCommand',
  echoDi: 'EchoDi',
  echoJson: 'EchoJson',
  echoJsonCommand: 'EchoJsonCommand',
  echoProtobuf: 'EchoProtobuf',
  echoProtobufCommand: 'EchoProtobufCommand',
  echoMessagePack: 'EchoMessagePack',
  echoMessagePackCommand: 'EchoMessagePackCommand'
} as const;

export interface EchoReq {
  readonly value: string;
}

export interface EchoReply {
  readonly value: string;
  readonly contentType: string;
}

export interface EchoCommand {
  readonly commandId: string;
  readonly value: string;
}

export class ProtobufEchoReq {
  constructor(readonly value: string) {}
}

export class ProtobufEchoCommand {
  constructor(readonly value: string) {}
}

export class MessagePackEchoReq {
  constructor(readonly value: string) {}
}

export class MessagePackEchoCommand {
  constructor(
    readonly commandId: string,
    readonly value: string
  ) {}
}

export interface CodecScenarioResult {
  readonly value?: string;
  readonly contentType?: string;
  readonly json?: EchoReply;
  readonly protobufValue?: string;
  readonly messagePackValue?: string;
}

export interface EvidenceWaitRequest {
  readonly containsAll: readonly string[];
  readonly timeoutMilliseconds?: number;
}
