# Latency probe and single-variable Carrier experiment

The default transfer behavior is unchanged: one RX URB, synchronous TX with its
existing 1000ms timeout, initial readiness/Carrier checks, periodic Carrier GET,
and the existing disconnect/cancellation quarantine. No logs run in the class
worker, ISR, or TX path. Elapsed clocks are wrapping monotonic milliseconds.

The RT-Thread application may call `ipheth_performance_report()` from a low
priority worker. It samples each attached interface every five seconds using a
nonblocking adapter lock, snapshots/resets timing counters in a short critical
section, releases the adapter lock, then prints short console lines. Metrics
include count, average, histogram p95 upper bound, and exact window maximum.
Bins are <=0, <=1, <=4, <=16, <=64, <=256, <=1000, >1000ms; the last bin reports
its observed maximum as the upper bound. These are wall times, not CPU times.

- `carrier`: the entire periodic/initial Carrier GET, including EP0 lock wait.
- `wake`: RX completion callback to worker consumption of that completion.
- `rearm`: RX callback to the start of the next successful submit; excludes
  idle time waiting for a new device packet and includes error recovery delays.
- `parse`: NCM parse plus all frame delivery callbacks (including any direct
  lwIP input/ACK processing configured by the host SDK).
- `tx_lock`: waiting for the class TX mutex, excluding the adapter's outer lock.
- `tx_urb`: the existing synchronous HCD submission, including completion wait.

`usbh_ipheth_take_probe()` drains one window; use only one reporter per device.
Existing cumulative `usbh_ipheth_get_stats()` counters are not reset. Normal
transfer error counters in the probe are cumulative and the RT-Thread reporter
computes deltas. A completion/operation spanning a boundary is accounted when
its timing measurement completes; windows are not synchronized with another
application's video counters.

With FINSH enabled:

```text
ipheth_probe carrier off
ipheth_probe carrier on
```

`off` skips periodic Carrier queries **after the initial Carrier query**; it
does not force link up or authorize TX before RX readiness. It temporarily
retains the last known Carrier state. Use only for an experiment; USB detach
still tears down normally and reconnect restores polling on. A caller owns the
same lifetime guarantee as for send/get_stats when invoking the setter.

Use a single attachment with sustained changing content: on for 30 seconds,
off for 30, on for 30, discard the first report after each switch. Compare
video RX cadence and windowed wake/rearm with Carrier time and TX duration.
If off removes Carrier samples but does not improve rearm/FPS, periodic Carrier
is not established as the main bottleneck. No hardware FPS gain is claimed.

The actual-worker host regression injects a 120ms Carrier operation, completes
an independent RX during it and observes delayed wake/rearm; it checks off/on,
slow synchronous TX timing, window reset, and the existing lifecycle/error cases.
This demonstrates the blocking mechanism, not its frequency on a real host.
# RX error classification (v26)

`rx_errors` counts failed completion-block processing, not individual lost
Ethernet frames. The diagnostic receive API preserves the original parser
checks, return codes and callback order. It separates size/signature/header/
block/table/DPE range/frame size/overlap/termination failures from callback
failure. A callback failure still stops delivery of subsequent datagrams, as
before; `callback_remaining` counts the current and subsequent validated but
not successfully delivered datagrams.

The five-second reporter emits nonzero reason counts and the last failure's
NTH16/NDP16 header fields (20 bytes), DPE index/offset/length and validated/
delivered counts. It never retains a DMA view or records Ethernet payload.
The RT-Thread adapter separately counts pbuf allocation, copy and netif input
failures, retaining the last input error code. No per-frame UART output is
added. Adapter and USB snapshots have slightly different boundaries, so
compare several windows rather than requiring exact single-window equality.

Linux's inspected NCM callback does not enforce every extra constraint in
this parser. A `block` or `table` failure is evidence to inspect a capture,
not authorization to drop bounds checks. Header corruption from an HCD/DMA
fault is another possible source. v26 changes no transfer, parser acceptance,
timeout, DATA PID or cancellation behavior.
