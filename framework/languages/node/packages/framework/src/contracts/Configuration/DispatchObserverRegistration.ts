import type { Type, ZLinkDispatchOptions, ZLinkMessageFlowObserver } from '../../contracts';

const observerTypes = new WeakMap<object, Type<ZLinkMessageFlowObserver>>();

export function setDispatchObserverType(
  dispatch: ZLinkDispatchOptions,
  observerType: Type<ZLinkMessageFlowObserver>
): void {
  observerTypes.set(dispatch, observerType);
}

export function getDispatchObserverType(
  dispatch: ZLinkDispatchOptions | undefined
): Type<ZLinkMessageFlowObserver> | undefined {
  return dispatch === undefined ? undefined : observerTypes.get(dispatch);
}
