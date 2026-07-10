import type {
  Type,
  ZLinkActor,
  ZLinkEntrySpot,
  ZLinkEntrySpotActorRequestHandlerRegistration,
  ZLinkEntrySpotActorSendHandlerRegistration,
  ZLinkEntrySpotPacketHandlerRegistration,
  ZLinkEntrySpotSubscriptionHandlerRegistration,
  ZLinkSpot,
  ZLinkSpotActorRequestHandlerRegistration,
  ZLinkSpotActorSendHandlerRegistration,
  ZLinkSpotHandlerRegistry,
  ZLinkSpotPacketHandlerRegistration,
  ZLinkSpotSubscriptionHandlerRegistration
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import {
  ZLinkActorPacketKind,
  ZLinkSpotActorHandlerRegistryRuntime
} from '../actors';

export interface ZLinkSpotHandlerRegistration {
  readonly kind:
    | 'handler'
    | 'packet'
    | 'subscribe'
    | 'actorSend'
    | 'actorRequest'
    | 'spotHandler';
  readonly handlerType: Type;
  readonly packetName?: string;
  readonly topic?: string;
  readonly actorType?: Type<ZLinkActor>;
}

export class DefaultZLinkSpotHandlerRegistry<TActor extends ZLinkActor = ZLinkActor> implements ZLinkSpotHandlerRegistry<TActor> {
  private readonly entries: ZLinkSpotHandlerRegistration[] = [];

  constructor(private readonly actorHandlers?: ZLinkSpotActorHandlerRegistryRuntime) {}

  addHandler(handlerType: Type): this {
    this.entries.push({ kind: 'handler', handlerType });
    return this;
  }

  addPacket(handlerType: Type, packetName?: string): this {
    this.entries.push({ kind: 'packet', handlerType, packetName });
    return this;
  }

  packet(packetName: string, handlerType: Type): this {
    return this.addPacket(handlerType, packetName);
  }

  addActorPacketRegistration(
    kind: ZLinkActorPacketKind,
    handlerType: Type,
    actorType: Type<TActor>,
    packetName: string
  ): this {
    this.entries.push({
      kind: kind === ZLinkActorPacketKind.Send ? 'actorSend' : 'actorRequest',
      handlerType,
      actorType,
      packetName
    });
    this.actorHandlers?.addPacket({
      kind,
      packetName,
      actorType,
      handlerType
    });
    return this;
  }

  actorSend(packetName: string, handlerType: Type, actorType: Type<TActor> = Object as unknown as Type<TActor>): this {
    return this.addActorPacketRegistration(
      ZLinkActorPacketKind.Send,
      handlerType,
      actorType,
      packetName
    );
  }

  actorRequest(packetName: string, handlerType: Type, actorType: Type<TActor> = Object as unknown as Type<TActor>): this {
    return this.addActorPacketRegistration(
      ZLinkActorPacketKind.Request,
      handlerType,
      actorType,
      packetName
    );
  }

  addSubscribe(handlerType: Type, topic: string): this {
    if (topic.trim().length === 0) {
      throw new ZLinkConfigurationException('SPOT subscribe topic must not be empty.');
    }
    this.entries.push({ kind: 'subscribe', handlerType, topic });
    return this;
  }

  subscribe(topic: string, handlerType: Type): this {
    return this.addSubscribe(handlerType, topic);
  }

  addSpotHandler(handlerType: Type): this {
    this.entries.push({ kind: 'spotHandler', handlerType });
    return this;
  }

  snapshot(): readonly ZLinkSpotHandlerRegistration[] {
    return [...this.entries];
  }
}

interface ZLinkEntrySpotHandlerRegistrationSet {
  readonly actorSendHandlers?: readonly ZLinkEntrySpotActorSendHandlerRegistration[];
  readonly actorRequestHandlers?: readonly ZLinkEntrySpotActorRequestHandlerRegistration[];
  readonly packetHandlers?: readonly ZLinkEntrySpotPacketHandlerRegistration[];
  readonly subscriptionHandlers?: readonly ZLinkEntrySpotSubscriptionHandlerRegistration[];
}

interface ZLinkUserSpotHandlerRegistrationSet {
  readonly actorSendHandlers?: readonly ZLinkSpotActorSendHandlerRegistration[];
  readonly actorRequestHandlers?: readonly ZLinkSpotActorRequestHandlerRegistration[];
  readonly packetHandlers?: readonly ZLinkSpotPacketHandlerRegistration[];
  readonly subscriptionHandlers?: readonly ZLinkSpotSubscriptionHandlerRegistration[];
}

export function applyEntrySpotHandlerRegistrations(
  registry: DefaultZLinkSpotHandlerRegistry,
  entrySpotType: Type<ZLinkEntrySpot>,
  registrations: ZLinkEntrySpotHandlerRegistrationSet
): void {
  for (const handler of registrations.actorSendHandlers ?? []) {
    if (handler.entrySpotType === entrySpotType) {
      registry.addActorPacketRegistration(
        ZLinkActorPacketKind.Send,
        handler.handlerType,
        handler.actorType,
        handler.packetName
      );
    }
  }
  for (const handler of registrations.actorRequestHandlers ?? []) {
    if (handler.entrySpotType === entrySpotType) {
      registry.addActorPacketRegistration(
        ZLinkActorPacketKind.Request,
        handler.handlerType,
        handler.actorType,
        handler.packetName
      );
    }
  }
  for (const handler of registrations.packetHandlers ?? []) {
    if (handler.entrySpotType === entrySpotType) {
      registry.addPacket(handler.handlerType, handler.packetName);
    }
  }
  for (const handler of registrations.subscriptionHandlers ?? []) {
    if (handler.entrySpotType === entrySpotType) {
      registry.addSubscribe(handler.handlerType, handler.topic);
    }
  }
}

export function applySpotHandlerRegistrations(
  registry: DefaultZLinkSpotHandlerRegistry,
  spotType: Type<ZLinkSpot>,
  registrations: ZLinkUserSpotHandlerRegistrationSet
): void {
  for (const handler of registrations.actorSendHandlers ?? []) {
    if (handler.spotType === spotType) {
      registry.addActorPacketRegistration(
        ZLinkActorPacketKind.Send,
        handler.handlerType,
        handler.actorType,
        handler.packetName
      );
    }
  }
  for (const handler of registrations.actorRequestHandlers ?? []) {
    if (handler.spotType === spotType) {
      registry.addActorPacketRegistration(
        ZLinkActorPacketKind.Request,
        handler.handlerType,
        handler.actorType,
        handler.packetName
      );
    }
  }
  for (const handler of registrations.packetHandlers ?? []) {
    if (handler.spotType === spotType) {
      registry.addPacket(handler.handlerType, handler.packetName);
    }
  }
  for (const handler of registrations.subscriptionHandlers ?? []) {
    if (handler.spotType === spotType) {
      registry.addSubscribe(handler.handlerType, handler.topic);
    }
  }
}
