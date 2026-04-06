#ifndef ZLINK_CPP_PERF_LATENCY_SAMPLER_HPP
#define ZLINK_CPP_PERF_LATENCY_SAMPLER_HPP

#include <algorithm>
#include <cstddef>
#include <vector>

namespace perf {

struct latency_sampler_stats_t
{
    latency_sampler_stats_t () : mean_us (0.0), p95_us (0.0), p99_us (0.0) {}

    double mean_us;
    double p95_us;
    double p99_us;
};

class latency_sampler_t
{
  public:
    explicit latency_sampler_t (size_t cap = 200000)
        : _cap (cap > 0 ? cap : 1),
          _count (0),
          _sum (0.0),
          _rng (0x9e3779b97f4a7c15ULL)
    {
        _samples.reserve (_cap);
    }

    void add (double latency_us)
    {
        const double sample = latency_us >= 0.0 ? latency_us : 0.0;
        ++_count;
        _sum += sample;

        if (_samples.size () < _cap) {
            _samples.push_back (sample);
            return;
        }

        const unsigned long long slot = next_rand () % _count;
        if (slot < static_cast<unsigned long long> (_cap))
            _samples[static_cast<size_t> (slot)] = sample;
    }

    void merge_from (const latency_sampler_t &other)
    {
        if (other._count == 0)
            return;

        _sum += other._sum;
        const unsigned long long merged_count = _count + other._count;
        unsigned long long seen = _count;
        for (size_t i = 0; i < other._samples.size (); ++i) {
            ++seen;
            const double sample = other._samples[i];
            if (_samples.size () < _cap) {
                _samples.push_back (sample);
                continue;
            }

            const unsigned long long slot = next_rand () % seen;
            if (slot < static_cast<unsigned long long> (_cap))
                _samples[static_cast<size_t> (slot)] = sample;
        }
        _count = merged_count;
    }

    unsigned long long count () const
    {
        return _count;
    }

    latency_sampler_stats_t snapshot ()
    {
        latency_sampler_stats_t out;
        if (_count == 0)
            return out;

        out.mean_us = _sum / static_cast<double> (_count);
        if (_samples.empty ()) {
            out.p95_us = out.mean_us;
            out.p99_us = out.mean_us;
            return out;
        }

        std::sort (_samples.begin (), _samples.end ());
        out.p95_us = percentile (_samples, 0.95);
        out.p99_us = percentile (_samples, 0.99);
        if (out.p95_us < out.mean_us)
            out.p95_us = out.mean_us;
        if (out.p99_us < out.p95_us)
            out.p99_us = out.p95_us;
        return out;
    }

  private:
    static double percentile (const std::vector<double> &values, double q)
    {
        if (values.empty ())
            return 0.0;
        if (q <= 0.0)
            return values.front ();
        if (q >= 1.0)
            return values.back ();

        const double pos = (values.size () - 1) * q;
        const size_t lo = static_cast<size_t> (pos);
        const size_t hi = lo + 1 < values.size () ? lo + 1 : lo;
        const double frac = pos - static_cast<double> (lo);
        return values[lo] + (values[hi] - values[lo]) * frac;
    }

    unsigned long long next_rand ()
    {
        if (_rng == 0)
            _rng = 0x9e3779b97f4a7c15ULL;
        unsigned long long x = _rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        _rng = x;
        return x;
    }

    size_t _cap;
    unsigned long long _count;
    double _sum;
    unsigned long long _rng;
    std::vector<double> _samples;
};

} // namespace perf

#endif
