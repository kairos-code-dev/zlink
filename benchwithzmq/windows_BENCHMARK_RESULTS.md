# Windows Benchmark Results - zlink vs libzmq

**Test Date:** 2026-01-12 05:07:05
**Platform:** Windows x64
**Compiler:** Visual Studio 2022 (MSVC 19.44.35222.0)
**Transport:** TCP

## Test Configuration

- **libzmq (reference)**: v4.3.5 with CURVE/libsodium enabled
- **zlink (optimized)**: v4.3.5 minimal build (CURVE disabled, no libsodium)
- **Message Count**: 200,000 (64-byte), 20,000 (1024-byte)
- **Warm-up iterations**: 1,000

## Results Summary

### 64-byte Messages

| Pattern | libzmq (msg/s) | zlink (msg/s) | Difference | libzmq (μs) | zlink (μs) | Difference |
|---------|---------------|--------------|------------|-------------|-----------|------------|
| **PAIR** | 5,890,166 | 5,089,434 | -13.6% | 21.60 | 22.11 | +2.4% |
| **PUB/SUB** | 5,859,861 | 4,688,375 | -20.0% | 0.17 | 0.21 | +23.5% |
| **DEALER/DEALER** | 6,025,095 | 5,104,906 | -15.3% | 21.94 | 21.79 | -0.7% |
| **DEALER/ROUTER** | 5,195,561 | 4,902,922 | -5.6% | 22.71 | 22.90 | +0.8% |
| **ROUTER/ROUTER** | 5,364,274 | 4,007,036 | -25.3% | 13.90 | 19.76 | +42.2% |

### 1024-byte Messages

| Pattern | libzmq (msg/s) | zlink (msg/s) | Difference | libzmq (μs) | zlink (μs) | Difference |
|---------|---------------|--------------|------------|-------------|-----------|------------|
| **PAIR** | 775,309 | 720,580 | -7.1% | 23.66 | 22.06 | -6.8% |
| **PUB/SUB** | 773,468 | 750,449 | -3.0% | 1.29 | 1.33 | +3.1% |

## Detailed Results

### PAIR Pattern (64-byte)
```
libzmq: Throughput=5,890,166 msg/s, Latency=21.60 μs
zlink:  Throughput=5,089,434 msg/s, Latency=22.11 μs
Performance: -13.6% throughput, +2.4% latency
```

### PUB/SUB Pattern (64-byte)
```
libzmq: Throughput=5,859,861 msg/s, Latency=0.17 μs
zlink:  Throughput=4,688,375 msg/s, Latency=0.21 μs
Performance: -20.0% throughput, +23.5% latency
```

### DEALER/DEALER Pattern (64-byte)
```
libzmq: Throughput=6,025,095 msg/s, Latency=21.94 μs
zlink:  Throughput=5,104,906 msg/s, Latency=21.79 μs
Performance: -15.3% throughput, -0.7% latency (slightly better)
```

### DEALER/ROUTER Pattern (64-byte)
```
libzmq: Throughput=5,195,561 msg/s, Latency=22.71 μs
zlink:  Throughput=4,902,922 msg/s, Latency=22.90 μs
Performance: -5.6% throughput, +0.8% latency
```

### ROUTER/ROUTER Pattern (64-byte)
```
libzmq: Throughput=5,364,274 msg/s, Latency=13.90 μs
zlink:  Throughput=4,007,036 msg/s, Latency=19.76 μs
Performance: -25.3% throughput, +42.2% latency
```

### PAIR Pattern (1024-byte)
```
libzmq: Throughput=775,309 msg/s, Latency=23.66 μs
zlink:  Throughput=720,580 msg/s, Latency=22.06 μs
Performance: -7.1% throughput, -6.8% latency (better)
```

### PUB/SUB Pattern (1024-byte)
```
libzmq: Throughput=773,468 msg/s, Latency=1.29 μs
zlink:  Throughput=750,449 msg/s, Latency=1.33 μs
Performance: -3.0% throughput, +3.1% latency
```

## Analysis

### Throughput Performance
- **Best Performance**: DEALER/ROUTER (-5.6%) - Most competitive pattern
- **Worst Performance**: ROUTER/ROUTER (-25.3%) - Largest gap
- **Average**: -12.8% throughput reduction across all patterns
- **Large Messages**: Better performance with 1024-byte messages (-5.1% average)

### Latency Performance
- **Wins**: DEALER/DEALER (-0.7%), PAIR 1024-byte (-6.8%)
- **Losses**: ROUTER/ROUTER (+42.2%), PUB/SUB (+23.5%)
- **Observation**: zlink shows better latency in specific scenarios, particularly with larger messages

### Binary Size Comparison
```
libzmq.dll (with CURVE): 492 KB
zlink.dll (minimal):     362 KB
Reduction:               130 KB (-26.4%)
```

## Environment

### System Information
- **OS**: Windows 10.0.26200
- **CPU**: x64
- **Compiler**: MSVC 19.44 (Visual Studio 2022)
- **Build Type**: Release with /O2 optimization

### Build Configuration
**libzmq (reference)**:
- CURVE encryption: ON
- libsodium: Statically linked
- Draft API: ON
- All socket types enabled

**zlink (minimal)**:
- CURVE encryption: OFF
- libsodium: Removed
- Draft API: OFF
- Limited socket types (PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER, STREAM)

## Conclusions

1. **Binary Size**: zlink achieves 26.4% smaller binary size by removing CURVE/libsodium support
2. **Performance Trade-off**: 5-25% throughput reduction depending on pattern
3. **Competitive Patterns**: DEALER/ROUTER and large message scenarios show minimal performance impact
4. **Latency Characteristics**: Some patterns (DEALER/DEALER, PAIR large messages) show better latency with zlink
5. **Use Case**: Suitable for applications where binary size and deployment simplicity matter more than maximum throughput

## Raw Data

### 64-byte Messages
```
RESULT,libzmq,PAIR,tcp,64,throughput,5890166.07
RESULT,libzmq,PAIR,tcp,64,latency,21.60
RESULT,zlink,PAIR,tcp,64,throughput,5089434.08
RESULT,zlink,PAIR,tcp,64,latency,22.11
RESULT,libzmq,PUBSUB,tcp,64,throughput,5859861.41
RESULT,libzmq,PUBSUB,tcp,64,latency,0.17
RESULT,zlink,PUBSUB,tcp,64,throughput,4688375.41
RESULT,zlink,PUBSUB,tcp,64,latency,0.21
RESULT,libzmq,DEALER_DEALER,tcp,64,throughput,6025094.52
RESULT,libzmq,DEALER_DEALER,tcp,64,latency,21.94
RESULT,zlink,DEALER_DEALER,tcp,64,throughput,5104905.81
RESULT,zlink,DEALER_DEALER,tcp,64,latency,21.79
RESULT,libzmq,DEALER_ROUTER,tcp,64,throughput,5195560.91
RESULT,libzmq,DEALER_ROUTER,tcp,64,latency,22.71
RESULT,zlink,DEALER_ROUTER,tcp,64,throughput,4902922.14
RESULT,zlink,DEALER_ROUTER,tcp,64,latency,22.90
RESULT,libzmq,ROUTER_ROUTER,tcp,64,throughput,5364274.47
RESULT,libzmq,ROUTER_ROUTER,tcp,64,latency,13.90
RESULT,zlink,ROUTER_ROUTER,tcp,64,throughput,4007036.36
RESULT,zlink,ROUTER_ROUTER,tcp,64,latency,19.76
```

### 1024-byte Messages
```
RESULT,libzmq,PAIR,tcp,1024,throughput,775309.19
RESULT,libzmq,PAIR,tcp,1024,latency,23.66
RESULT,zlink,PAIR,tcp,1024,throughput,720579.72
RESULT,zlink,PAIR,tcp,1024,latency,22.06
RESULT,libzmq,PUBSUB,tcp,1024,throughput,773468.48
RESULT,libzmq,PUBSUB,tcp,1024,latency,1.29
RESULT,zlink,PUBSUB,tcp,1024,throughput,750449.33
RESULT,zlink,PUBSUB,tcp,1024,latency,1.33
```

---
Generated by zlink benchmark suite
