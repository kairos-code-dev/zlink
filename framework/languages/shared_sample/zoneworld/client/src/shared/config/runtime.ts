export interface ClientEndpoints {
  gateway: string;
  ops: string;
}

export function loadClientEndpoints(location: Location = window.location): ClientEndpoints {
  const query = new URLSearchParams(location.search);
  return {
    gateway: query.get('gateway') ?? import.meta.env.VITE_ZONEWORLD_GATEWAY ?? 'ws://127.0.0.1:48080',
    ops: query.get('ops') ?? import.meta.env.VITE_ZONEWORLD_OPS ?? 'ws://127.0.0.1:48090',
  };
}
