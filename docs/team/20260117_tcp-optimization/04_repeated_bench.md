# Repeated TCP Bench (5 runs)

## Commands

```
python3 - <<'PY'
import subprocess, re, statistics
patterns = ["pair","pubsub","dealer_dealer","dealer_router","router_router","router_router_poll"]
lib_cmd = "./build/bin/comp_std_zmq_{p} libzmq tcp 64"
zlink_cmd = "./build/bin/comp_zlink_{p} zlink tcp 64"
run_count = 5
results = {"zlink": {p: [] for p in patterns}, "libzmq": {p: [] for p in patterns}}
throughput_re = re.compile(r"throughput,([0-9.]+)")

def run_one(cmd):
    out = subprocess.check_output(cmd, shell=True, text=True, stderr=subprocess.STDOUT)
    m = throughput_re.search(out)
    if not m:
        raise RuntimeError(f"no throughput in output for {cmd}\n{out}")
    return float(m.group(1))

for run in range(run_count):
    for p in patterns:
        v = run_one(f"BENCH_MSG_COUNT=10000 timeout 20 {zlink_cmd.format(p=p)}")
        results["zlink"][p].append(v)
    for p in patterns:
        v = run_one(f"BENCH_MSG_COUNT=10000 timeout 20 {lib_cmd.format(p=p)}")
        results["libzmq"][p].append(v)

for lib in ("zlink","libzmq"):
    print(lib)
    for p in patterns:
        vals = results[lib][p]
        avg = statistics.mean(vals)
        stdev = statistics.pstdev(vals)
        print(f"{p},{','.join(f'{v:.2f}' for v in vals)},avg,{avg:.2f},stdev,{stdev:.2f}")
PY
```

## Results (10K, 64B)

### zlink

- PAIR avg 5.24 M/s (stdev 0.11)
- PUBSUB avg 5.24 M/s (stdev 0.07)
- DEALER_DEALER avg 5.24 M/s (stdev 0.23)
- DEALER_ROUTER avg 4.64 M/s (stdev 0.11)
- ROUTER_ROUTER avg 4.32 M/s (stdev 0.13)
- ROUTER_ROUTER_POLL avg 4.15 M/s (stdev 0.08)

### libzmq

- PAIR avg 5.51 M/s (stdev 0.27)
- PUBSUB avg 5.17 M/s (stdev 0.12)
- DEALER_DEALER avg 5.42 M/s (stdev 0.20)
- DEALER_ROUTER avg 4.92 M/s (stdev 0.10)
- ROUTER_ROUTER avg 4.68 M/s (stdev 0.11)
- ROUTER_ROUTER_POLL avg 4.46 M/s (stdev 0.21)

## Raw Samples

zlink
- pair: 5055251.37, 5197021.69, 5332175.90, 5235284.14, 5366825.18
- pubsub: 5141047.20, 5257593.15, 5210797.61, 5250435.26, 5343338.91
- dealer_dealer: 5039855.17, 5377023.44, 5114882.83, 5042442.24, 5622728.42
- dealer_router: 4732502.40, 4577004.67, 4805448.99, 4517946.41, 4555142.51
- router_router: 4414601.74, 4261408.43, 4468313.18, 4343818.44, 4093694.85
- router_router_poll: 4117883.47, 4108505.63, 4065600.91, 4300453.78, 4166965.30

libzmq
- pair: 5683280.98, 5601741.92, 5317064.53, 5091641.92, 5840773.51
- pubsub: 5003187.03, 5170665.57, 5367850.76, 5095113.02, 5221561.29
- dealer_dealer: 5542361.66, 5354634.78, 5471662.81, 5074583.69, 5643828.16
- dealer_router: 4889487.80, 4783004.65, 5072076.75, 4965699.43, 4896895.86
- router_router: 4559158.96, 4766855.48, 4676546.65, 4828167.92, 4574653.31
- router_router_poll: 4624972.54, 4310318.82, 4550077.01, 4121420.34, 4674218.65

## Notes

- ROUTER_ROUTER_POLL (zlink) logs debug lines in the current bench code.
- Variance exists; averages should be used for comparison.
