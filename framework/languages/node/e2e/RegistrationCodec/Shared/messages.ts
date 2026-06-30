export const RegistrationCodecNames = {
  channel: 'reg-codec'
} as const;

export const PacketNames = {
  echoAutoReq: 'EchoAutoReq',
  echoAutoMsg: 'EchoAutoMsg',
  echoAttrReq: 'EchoAttrReq',
  echoAttrMsg: 'EchoAttrMsg',
  echoManualReq: 'EchoManualReq',
  echoManualMsg: 'EchoManualMsg',
  echoDiReq: 'EchoDiReq',
  echoJsonReq: 'EchoJsonReq',
  echoJsonMsg: 'EchoJsonMsg',
  echoProtobufReq: 'EchoProtobufReq',
  echoProtobufMsg: 'EchoProtobufMsg',
  echoMessagePackReq: 'EchoMessagePackReq',
  echoMessagePackMsg: 'EchoMessagePackMsg'
} as const;

export interface EchoReq {
  readonly value: string;
}

export interface EchoRes {
  readonly value: string;
  readonly contentType: string;
}

export interface EchoMsg {
  readonly commandId: string;
  readonly value: string;
}

export class ProtobufEchoReq {
  constructor(readonly value: string) {}
}

export class ProtobufEchoMsg {
  constructor(readonly value: string) {}
}

export class MessagePackEchoReq {
  constructor(readonly value: string) {}
}

export class MessagePackEchoMsg {
  constructor(
    readonly commandId: string,
    readonly value: string
  ) {}
}

export interface CodecScenarioRes {
  readonly value?: string;
  readonly contentType?: string;
  readonly json?: EchoRes;
  readonly protobufValue?: string;
  readonly messagePackValue?: string;
}

export interface EvidenceWaitReq {
  readonly containsAll: readonly string[];
  readonly timeoutMilliseconds?: number;
}
