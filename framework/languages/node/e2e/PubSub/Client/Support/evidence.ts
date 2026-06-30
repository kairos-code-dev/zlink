export function isEvent(line: string, runId: string, topic: string): boolean {
  return line.includes('event|') && line.includes(`run=${runId}`) && line.includes(`topic=${topic}`);
}

export function extractInt(line: string, key: string): number {
  const marker = `${key}=`;
  const start = line.indexOf(marker);
  if (start < 0) {
    return 0;
  }
  const valueStart = start + marker.length;
  const end = line.indexOf('|', valueStart);
  return Number.parseInt(end < 0 ? line.slice(valueStart) : line.slice(valueStart, end), 10);
}

export function commonContiguousSequence(
  snapshots: readonly (readonly string[])[],
  runId: string,
  topic: string,
  min: number,
  max: number
): readonly number[] {
  const sets = snapshots.map((lines) => new Set(
    lines
      .filter((line) => isEvent(line, runId, topic))
      .map((line) => extractInt(line, 'seq'))
      .filter((seq) => seq >= min && seq <= max)
  ));
  if (sets.length === 0) {
    return [];
  }
  const common = new Set(sets[0]);
  for (const set of sets.slice(1)) {
    for (const seq of [...common]) {
      if (!set.has(seq)) {
        common.delete(seq);
      }
    }
  }
  const sorted = [...common].sort((left, right) => left - right);
  let best: number[] = [];
  let current: number[] = [];
  for (const seq of sorted) {
    if (current.length === 0 || current[current.length - 1] + 1 === seq) {
      current.push(seq);
    } else {
      if (current.length > best.length) {
        best = current;
      }
      current = [seq];
    }
  }
  return current.length > best.length ? current : best;
}
