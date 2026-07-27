import type {
  Type,
  ZLinkActor,
  ZLinkActorTransferAdapter,
  ZLinkMessageSerializer,
} from '../../contracts';
import type { ZLinkProviderResolver } from '../../contracts/Common/ZLinkProviderResolver';
import { ZLinkEncodedPayload, ZLinkMessage } from '../../contracts';
import { throwIfAborted } from '../abort';

export interface ZLinkActorTransferPayloadState {
  readonly adapterKey?: string;
  readonly state: ZLinkMessage;
}

export class ZLinkActorTransferRegistry {
  private readonly byActorType = new Map<Type, Type>();
  private readonly byKey = new Map<string, Type>();

  constructor(
    registrations: ReadonlyMap<Type, Type>,
    private readonly providerResolver?: ZLinkProviderResolver,
    private readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>,
    private readonly actorFactories: ReadonlyMap<string, Type> = new Map()
  ) {
    for (const [actorType, adapterType] of registrations) {
      const key = actorTransferKey(actorType);
      if (this.byKey.has(key)) {
        throw new Error(`Actor transfer type key '${key}' is registered more than once.`);
      }
      this.byActorType.set(actorType, adapterType);
      this.byKey.set(key, adapterType);
    }
  }

  async transferOut(
    actor: ZLinkActor,
    actorType: string | undefined,
    signal?: AbortSignal
  ): Promise<ZLinkActorTransferPayloadState> {
    throwIfAborted(signal);
    const registeredType = actorType === undefined
      ? actor.constructor as Type
      : this.actorFactories.get(actorType) ?? actor.constructor as Type;
    const adapterType = this.byActorType.get(registeredType);
    if (adapterType === undefined) {
      return { state: emptyTransferState(this.messageSerializers) };
    }
    const adapter = await this.createAdapter(adapterType) as ZLinkActorTransferAdapter<ZLinkActor>;
    const state = await adapter.transferOut(actor);
    if (!(state instanceof ZLinkMessage)) {
      throw new Error(`Actor transfer adapter '${adapterType.name}' returned an invalid state message.`);
    }
    return { adapterKey: actorTransferKey(registeredType), state };
  }

  async transferIn(
    adapterKey: string,
    actorId: string,
    state: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<ZLinkActor> {
    throwIfAborted(signal);
    const adapterType = this.byKey.get(adapterKey);
    if (adapterType === undefined) {
      throw new Error(`Actor transfer adapter '${adapterKey}' is not registered on the target node.`);
    }
    const adapter = await this.createAdapter(adapterType) as ZLinkActorTransferAdapter<ZLinkActor>;
    const actor = await adapter.transferIn(actorId, state);
    return actor;
  }

  private async createAdapter(adapterType: Type): Promise<unknown> {
    const existing = this.providerResolver?.get?.(adapterType);
    if (existing !== undefined) {
      return existing;
    }
    const created = await this.providerResolver?.create?.(adapterType);
    if (created !== undefined) {
      return created;
    }
    return new (adapterType as new () => unknown)();
  }
}

function actorTransferKey(actorType: Type): string {
  const key = actorType.name.trim();
  if (key.length === 0) {
    throw new Error('Actor transfer types must have a stable class name.');
  }
  return key;
}

function emptyTransferState(
  _serializers?: ReadonlyMap<string, ZLinkMessageSerializer>
): ZLinkMessage {
  return ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(Buffer.alloc(0)));
}
