# Stratum TLS Support Notes

These notes outline a possible implementation for Stratum-over-TLS on the
target hardware: ESP32-WROOM-32 using the classic ESP32 target, no PSRAM, and
the current 4 MB flash layout.

## Current Baseline

Measured after `./scripts/build-firmware.sh` on the current tree:

- App binary: 763.27 KiB of 1.46 MiB, with 736.73 KiB free.
- IRAM: 106.26 KiB of 128.00 KiB, with 21.74 KiB free.
- Static DRAM: 65.73 KiB of 176.50 KiB, with 110.77 KiB free.
- `CONFIG_SPIRAM` is off.
- TLS is explicitly disabled in `sdkconfig.defaults`.
- If TLS is enabled with current defaults, mbedTLS is configured for a 16 KiB
  incoming fragment buffer and a 4 KiB outgoing fragment buffer.

Flash headroom is acceptable. Runtime heap and heap fragmentation are the main
constraints.

## Fit Assessment

Stratum-over-TLS should fit if it is implemented as a constrained client-only
feature. It should not be treated as a broad HTTPS/TLS enablement.

Required constraints:

- TLS client only.
- One live TLS connection at a time.
- No HTTPS server.
- No full root certificate bundle.
- No simultaneous primary/backup TLS probes.
- Hardware validation on ESP32-WROOM-32 before release.

## Build Configuration

The current defaults intentionally trim TLS:

- `CONFIG_MBEDTLS_TLS_DISABLED=y`
- `CONFIG_MBEDTLS_TLS_CLIENT=n`
- `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=n`
- `CONFIG_MBEDTLS_PEM_PARSE_C=n`
- `CONFIG_MBEDTLS_SSL_PROTO_TLS1_3=n`

A prototype should enable the smallest useful TLS client profile:

- Select TLS client-only mode, not server-and-client mode.
- Keep TLS 1.3 disabled unless a target pool requires it.
- Keep TLS server support disabled.
- Keep HTTPS server disabled.
- Keep ESP HTTP client HTTPS disabled unless that code path is actually used.
- Keep the root certificate bundle disabled unless there is no pinned-cert
  alternative.
- Prefer DER certificate data so PEM parsing can stay disabled.

Potential buffer tuning after compatibility testing:

- Reduce `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` if the selected pool never sends
  large TLS records.
- Reduce `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN` if Stratum writes remain small.
- Consider `CONFIG_MBEDTLS_DYNAMIC_BUFFER` only after measuring heap behavior
  on hardware; it may reduce steady-state memory but needs handshake testing.

Do not assume smaller TLS buffers are safe until the target pool has been tested.
Too-small buffers can fail during handshake or on larger records.

## Transport Design

The current Stratum code passes a raw socket `int` through connect, send, recv,
select, and close paths. TLS support should introduce a small transport wrapper
instead of spreading TLS conditionals through protocol code.

Suggested shape:

```c
typedef enum {
  STRATUM_TRANSPORT_TCP = 0,
  STRATUM_TRANSPORT_TLS,
} stratum_transport_mode_t;

typedef struct {
  stratum_transport_mode_t mode;
  int sock;
  esp_tls_t* tls;
} stratum_transport_t;
```

Wrapper operations:

- `stratum_transport_connect(...)`
- `stratum_transport_send_all(...)`
- `stratum_transport_recv(...)`
- `stratum_transport_wait_readable(...)`
- `stratum_transport_close(...)`

Protocol helpers such as subscribe, authorize, submit, and pong should accept
the transport wrapper rather than a raw socket. Parsing and mining logic should
not know whether the connection is TCP or TLS.

## Endpoint Settings

The UI and NVS settings need a way to distinguish cleartext Stratum from TLS.
Options:

- Add a boolean `use_tls` per endpoint.
- Infer TLS from a URI scheme such as `stratum+tcp://` or `stratum+tls://`.
- Infer TLS from common ports, but only as a convenience and not as the stored
  source of truth.

The safest settings model is explicit per endpoint:

- Primary host
- Primary port
- Primary TLS enabled
- Backup host
- Backup port
- Backup TLS enabled

Keep existing cleartext defaults unchanged unless the default pool is confirmed
to support TLS reliably.

## Certificate Strategy

Avoid the ESP-IDF full root certificate bundle on ESP32-WROOM-32 unless there is
no practical alternative. It adds flash use and increases TLS verification
surface.

Preferred options:

1. Pin the pool certificate public key.
2. Pin one compact CA certificate in DER format.
3. Ship a tiny custom certificate bundle containing only required roots.

PEM support should remain disabled if certificates are embedded as DER bytes.

Certificate rotation is the tradeoff:

- Public-key pinning is smallest but requires firmware changes when keys rotate.
- CA pinning is a little broader but more tolerant of leaf certificate renewals.
- A custom bundle is most flexible but costs more space and needs maintenance.

## Time Handling

Certificate validation needs a trustworthy wall clock. The current firmware uses
uptime timers for runtime behavior, not real wall-clock time.

Possible approaches:

- Start SNTP after station Wi-Fi connects and wait for time before TLS connect.
- Allow a short "time not ready" retry state in Stratum connection handling.
- If pinning a public key and deliberately skipping date validation, document
  that security tradeoff clearly.

SNTP should be station-mode only. Setup AP mode should not block on time sync.

## Primary Pool Probe

The current code can run a primary-pool reachability probe while connected to the
backup pool. With TLS, a second simultaneous TLS handshake is the highest-risk
heap scenario.

Recommended behavior:

- Keep the probe TCP-only if it is only checking reachability.
- Or disable primary probing while connected over TLS.
- Or close the backup session before attempting a TLS primary probe.

Do not maintain two TLS sessions at once on ESP32-WROOM-32.

## Mining Interaction

The miner uses the ESP32 hardware SHA path. mbedTLS can also use hardware crypto
accelerators. TLS handshake should not run while the miner is aggressively using
the same hardware resources.

Recommended behavior:

- Clear or pause mining work before TLS handshake.
- Resume mining only after subscribe/authorize and job setup.
- Measure whether accepted hashrate dips during reconnects.
- Keep the Stratum session task stack under review if TLS code is called from it.

## Runtime Memory Checks

Hardware validation must use the existing `/api/memory` endpoint and watch:

- `free_heap`
- `min_free_heap`
- `largest_free_block`
- `internal_free_heap`
- `internal_min_free_heap`
- `internal_largest_free_block`
- `dma_free_heap`

Test cases:

- Fresh boot, Wi-Fi connected, cleartext Stratum connected.
- Fresh boot, Wi-Fi connected, TLS Stratum connected.
- Repeated TLS reconnects for at least 30 minutes.
- Backup failover and primary recovery behavior.
- Web UI open while TLS session is active.
- `/api/stats` polling while TLS session is active.
- Setup AP mode after factory reset.
- Low Wi-Fi signal or intermittent network disconnects.

Pass criteria should include stable reconnects, no task watchdog resets, no
heap exhaustion, and no long-term decline in `largest_free_block`.

## Open Questions

- Which pools and ports must be supported over TLS?
- Should TLS be optional per endpoint or globally enabled?
- Is certificate pinning acceptable operationally?
- Do we need browser UI migration for existing saved pool settings?
- What minimum heap and largest-free-block thresholds should block TLS from
  starting?
