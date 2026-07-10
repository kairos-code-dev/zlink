const zlinkSocketTraceIds = new WeakMap<object, number>();
let zlinkNextSocketTraceId = 1;

export function socketTraceId(socket: object): number {
  let id = zlinkSocketTraceIds.get(socket);
  if (id === undefined) {
    id = zlinkNextSocketTraceId;
    zlinkNextSocketTraceId += 1;
    zlinkSocketTraceIds.set(socket, id);
  }
  return id;
}
