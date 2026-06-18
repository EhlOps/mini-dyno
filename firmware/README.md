# mini-dyno firmware

Firmware for the **mini-dyno**, a small water-brake dynamometer. It runs on an
**ESP32-C3**, stands up an open Wi-Fi access point, and serves live torque/RPM
telemetry to connected clients over a tiny built-in MQTT broker. A phone or
laptop joins the AP, subscribes, and plots the dyno run in real time.

Written in async Rust (`no_std`) on [`esp-hal`] and [`embassy`].

[`esp-hal`]: https://docs.rs/esp-hal
[`embassy`]: https://embassy.dev

## Read the docs (the guided tour)

The whole codebase is documented as rustdoc — the module headers read top to
bottom as an explanation of *why* the firmware is shaped the way it is. The
best way to understand it is to generate the docs and browse them:

```sh
cargo doc --no-deps --open
```

This builds the API docs for just this crate and opens them in your browser
(`target/riscv32imc-unknown-none-elf/doc/firmware/index.html`). Drop
`--no-deps` if you also want the docs for `esp-hal`, `embassy`, `smoltcp`, etc.
linked inline — that's a much bigger build, but every type cross-links to its
upstream definition.

Suggested reading order, following the links as you go:

1. **`firmware`** (crate root) — the one-paragraph map of the modules.
2. **`firmware::net`** — bringing up the Wi-Fi AP and the `embassy-net` stack;
   start with the `Net` type.
3. **`firmware::net::mqtt`** — the payload-agnostic MQTT 3.1.1/5 broker, its
   concurrency model (`Feed` / `Watch`), and the per-connection state machine.
4. **`firmware::drivers`** — the sensor drivers and their read loops:
   `drivers::hx711` (load-cell ADC, bit-banged) and `drivers::hall_effect`
   (interrupt-driven RPM pickup), each republishing on a `Signal`.
5. **`firmware::telemetry`** — the `Telemetry` sample type, its JSON wire
   format, and the producer task that latches the driver signals and feeds the
   broker.
6. **`firmware::macros`** — `mk_static!`, the `'static`-allocation helper the
   embassy tasks lean on.

> The binary entry point (`src/bin/main.rs`) is a separate bin crate, so it
> isn't part of the `firmware` library docs above. Its boot sequence is
> documented in the file's own `//!` header.

## Build & flash

Prerequisites:

- A Rust toolchain — the pinned channel and the RISC-V target are declared in
  [`rust-toolchain.toml`](rust-toolchain.toml) and installed automatically by `rustup`.
- [`espflash`](https://github.com/esp-rs/espflash) (`cargo install espflash`)
  for flashing over USB; it's already wired up as the cargo runner.

```sh
cargo build --release        # compile
cargo run                    # flash + monitor (defmt) over USB
cargo run --release          # same, optimized
```

The target (`riscv32imc-unknown-none-elf`) and the `espflash` runner are
configured in [`.cargo/config.toml`](.cargo/config.toml), so plain `cargo run`
just works. You can also simulate the firmware without hardware via
[Wokwi](https://wokwi.com) — see [`wokwi.toml`](wokwi.toml) and
[`diagram.json`](diagram.json).

## Connecting a client

1. Join the open Wi-Fi network **`mini-dyno`**.
2. Point an MQTT client at `192.168.1.1:1883` and subscribe to `dyno/telemetry`.
3. You'll receive a JSON payload per sample: `{"torque":<n.nn>,"rpm":<n>}`.

## Status

Both sensors are wired in and read live: the hall-effect driver
(`drivers::hall_effect`, GPIO10) reports real RPM, and the HX711 load-cell
driver (`drivers::hx711`, GPIO6/7) streams real ADC samples. **Calibration is
still pending** — `telemetry::producer` currently passes the raw 24-bit HX711
word straight through as the `torque` field, so that number is a placeholder
until an offset/scale (tare + N·m per count) is applied.
