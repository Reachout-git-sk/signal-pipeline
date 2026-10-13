# Multithreaded Signal Pipeline

A C++17 program for Linux. It receives sensor samples over UDP, batches them
into fixed-size windows, and fans the windows out to a pool of worker threads.
Each worker runs a hand-written iterative radix-2 FFT and reports the dominant
frequencies. The receiver and workers are connected by a bounded, thread-safe
queue built on `std::mutex` and `std::condition_variable`.

```
tools/sender.py ──UDP──▶ UdpReceiver ──Window (std::move)──▶ BoundedQueue<Window> ──▶ WorkerPool (N threads)
 50 Hz + 120 Hz          1 thread, batches                    mutex + 2 condvars          each: Hann window → FFT →
 + noise @ 1 kHz         samples into 1024-sample windows      blocks when full / empty     parabolic peak fit → top-k
```

```
$ ./build/signal_pipeline run --port 9000 --workers 4 &
$ python3 tools/sender.py --port 9000 --rate 1000 --freqs 50,120 --amps 1,0.5 --noise 0.2
window      0:    50.00 Hz (0.995)   120.01 Hz (0.497)
window      1:    50.00 Hz (1.002)   119.99 Hz (0.503)
...
```

## Design

| Piece | File | Notes |
|---|---|---|
| FFT | `src/fft.{h,cpp}` | In-place iterative Cooley–Tukey: bit-reversal permutation, then log2(n) butterfly stages, O(n log n). `SpectrumAnalyzer` applies a Hann window, builds the single-sided magnitude spectrum, and places peaks between bins with a parabolic fit. |
| Queue | `src/bounded_queue.h` | MPMC FIFO with a fixed capacity. `push` waits on `not_full`, `pop` waits on `not_empty`, `close()` wakes everyone. Pushes then fail, and pops drain what's left before returning `nullopt`. Locks are RAII (`std::unique_lock` for the waits, `std::lock_guard` elsewhere). |
| Receiver | `src/receiver.{h,cpp}` | Owns the UDP socket (closed in its destructor). Polls with a 100 ms timeout so it can stop cleanly. Validates packets, counts sequence gaps as lost packets, and appends samples to the current window. Each full window is **moved** into the queue. |
| Workers | `src/worker.{h,cpp}` | N `std::thread`s. Each owns its own `SpectrumAnalyzer`, so the FFT buffers aren't shared and the only shared state is the queue. |
| Packet | `src/packet.{h,cpp}` | `'SP01' magic, uint32 seq, uint16 count, uint16 reserved, float32[count]`, little endian, at most 360 samples per packet (under a 1500-byte MTU). |
| Sender | `tools/sender.py` | Sine waves plus Gaussian noise, paced to real time, or `--fast` for as fast as possible. |

**Back-pressure:** if the workers fall behind, the queue fills and the receiver
blocks in `push`. Incoming datagrams then wait in the kernel's socket buffer
(4 MB requested). If that overflows too, packets are dropped, and the sequence
numbers make every drop visible in the `lost packets` counter.

## Benchmarks

`./build/signal_pipeline bench ...`, measured in an Ubuntu 24.04 Docker
container on an 8-thread Windows laptop (`tools/run_benchmarks.sh`). Other
builds were running on the machine at the same time, so absolute numbers moved
between runs. The shape of the results held in every run.

### Lock contention: batching samples before enqueueing

One producer, 4 consumers, 8,388,608 samples, queue capacity 64. Consumers only
sum the samples, so the time is almost entirely queue synchronisation.

| Samples per queue item | Queue operations | Time | Samples/s |
|---|---|---|---|
| 1 (no batching) | 8,388,608 | 238.8 s | 35 thousand |
| 16 | 524,288 | 11.2 s | 0.75 million |
| 64 | 131,072 | 3.9 s | 2.1 million |
| 256 | 32,768 | 1.0 s | 8.0 million |
| **1024 (one window)** | **8,192** | **0.30 s** | **28.0 million** |

With one lock round trip per sample, every sample pays for a mutex handoff and
a condition variable wake-up. Batching 1024 samples per window cuts queue
operations 1024× and raised throughput about **800×**. That's why the receiver
only enqueues whole windows.

### FFT throughput vs worker threads

40,000 windows of 1024 samples (50 Hz + 120 Hz), queue capacity 64.

| Workers | Run 1 windows/s | Run 1 speedup | Speedup over 3 later runs |
|---|---|---|---|
| 1 | 14,820 | 1.00× | 1.00× |
| 2 | 32,178 | 2.17× | 1.50 to 1.70× |
| 4 | 67,625 | 4.56× | 3.28 to 3.69× |
| 8 | 71,796 | 4.84× | 3.78 to 4.73× |

Scaling is close to linear up to 4 workers and flattens at 8. The machine has
8 hardware threads, but the producer thread also needs a core. With 8 workers
the threads compete for cores, and the single producer filling the queue
becomes the limit.

## Correctness tooling

- **GoogleTest: 24 tests**, all passing.
  - FFT against a naive O(n²) DFT on random input, plus impulse, DC and Parseval checks.
  - A pure 50 Hz sine peaks at 50 Hz with no other peaks above 5%.
  - 50 Hz + 120 Hz with noise gives both peaks, in the right order and at the right ratio.
  - Between-bin interpolation, and rejecting sizes that aren't a power of two.
  - Queue: FIFO order, move-only items, blocking on full, close waking blocked producers and consumers, 8 producers × 8 consumers × 20,000 items with no loss or duplication, and per-producer ordering.
  - Packet round trip and malformed packets.
  - Receiver over real loopback UDP: in-order windows, gap counting, clean stop.
  - The full pipeline end to end over UDP.
- **Valgrind memcheck**: 0 errors, and "All heap blocks were freed -- no leaks are possible" (`docs/valgrind/memcheck_tests.txt`).
- **Valgrind DRD**: 0 errors on the queue and pipeline tests (`docs/valgrind/drd_tests.txt`).
- **Valgrind Helgrind**: 0 errors (`docs/valgrind/helgrind_tests.txt`).
  - Getting here took one code change. The queue originally notified *after* releasing the lock. That's correct, but Helgrind and DRD flag it, so it now notifies while holding the lock.
  - `tools/helgrind.supp` suppresses one known false positive. It covers only reports whose top frame is inside glibc's own `__pthread_cond_wait_common`: Helgrind misreads the internal signalling of glibc's newer condition variable code.
- **gdb**: `docs/gdb_session.txt` shows a breakpoint on `sp::fft_inplace` hit from the 50 Hz test, the backtrace (`TestBody → top_peaks → magnitudes → fft_inplace`), and the 1024-sample Hann-windowed input in the debugger.

CI (GitHub Actions, Ubuntu 24.04) builds, runs the tests, runs memcheck and
Helgrind, and runs short benchmarks on every push.

## Build and run

```bash
sudo apt-get install -y build-essential cmake ninja-build libgtest-dev valgrind gdb
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure

./build/signal_pipeline run --port 9000 --workers 4 --window 1024 --rate 1000
python3 tools/sender.py --port 9000 --seconds 10           # in another terminal

./build/signal_pipeline bench fft --workers 1,2,4,8 --windows 40000
./build/signal_pipeline bench batching --samples 8388608 --consumers 4

valgrind --tool=memcheck --leak-check=full ./build/sp_tests
valgrind --tool=helgrind --suppressions=tools/helgrind.supp ./build/sp_tests --gtest_filter='BoundedQueue.*:Pipeline.*'
```

On Windows or macOS, use the dev container: `docker build -t sp-dev -f docker/Dockerfile.dev docker`
and then `docker run --rm -it -v "$PWD":/src sp-dev`.
