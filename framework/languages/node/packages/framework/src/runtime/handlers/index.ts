import {
  type Type,
  type ZLinkHandlerDelegate,
  type ZLinkHandlerFilter,
  type ZLinkMessageContext
} from '../../contracts';
import {
  readZLinkDecoratorMetadata,
  type ZLinkDecoratorMetadata
} from '../../contracts/Handlers/Attributes';

export interface ZLinkHandlerDescriptor {
  readonly handlerType: Type;
  readonly metadata: ZLinkDecoratorMetadata;
}

export interface ZLinkHandlerExposurePolicy {
  readonly handlerGroups?: readonly string[];
  readonly explicitHandlers?: readonly Type[];
}

export function scanZLinkHandlerTypes(handlerTypes: readonly Type[]): readonly ZLinkHandlerDescriptor[] {
  return handlerTypes.flatMap((handlerType) =>
    readZLinkDecoratorMetadata(handlerType)
      .filter((metadata) => metadata.kind !== 'handlerGroup' && metadata.kind !== 'packet')
      .map((metadata) => ({ handlerType, metadata }))
  );
}

export function exposeZLinkHandlers(
  handlerTypes: readonly Type[],
  policy: ZLinkHandlerExposurePolicy
): readonly ZLinkHandlerDescriptor[] {
  const explicit = new Set(policy.explicitHandlers ?? []);
  const groups = new Set(policy.handlerGroups ?? []);

  return scanZLinkHandlerTypes(handlerTypes).filter((descriptor) => {
    if (explicit.has(descriptor.handlerType)) {
      return true;
    }
    if (groups.size === 0) {
      return false;
    }
    return readZLinkDecoratorMetadata(descriptor.handlerType).some(
      (metadata) => metadata.kind === 'handlerGroup' && metadata.groupName !== undefined && groups.has(metadata.groupName)
    );
  });
}

export async function invokeZLinkHandlerFilters(
  filters: readonly ZLinkHandlerFilter[],
  context: ZLinkMessageContext,
  terminal: ZLinkHandlerDelegate,
  signal?: AbortSignal
): Promise<unknown> {
  let next: ZLinkHandlerDelegate = terminal;

  for (let index = filters.length - 1; index >= 0; index -= 1) {
    const filter = filters[index];
    const previous = next;
    next = () => filter.invoke(context, previous, signal);
  }

  return next();
}
