#![no_std]

//! Firmware for the mini-dyno project.
//!
//! This is the core library crate; the binary entry point lives in
//! `src/bin/main.rs`, which initializes the hardware and wires the pieces below
//! together.
//!
//! Built on [`esp-hal`](esp_hal) and [`embassy`](embassy_executor), it is split
//! into:
//! - [`net`]: the Wi-Fi access point, the `embassy-net` stack, and the MQTT
//!   broker ([`net::mqtt`]) that serves connected clients.
//! - [`drivers`]: the sensor drivers ([`drivers::hx711`] load cell,
//!   [`drivers::hall_effect`] RPM pickup) and the tasks that read them.
//! - [`telemetry`]: the dynamometer sample type and the producer task that
//!   feeds torque/RPM measurements to the broker.
//! - [`macros`]: small `'static`-allocation helpers ([`mk_static!`]).

extern crate alloc;

pub mod net;
pub mod macros;
pub mod telemetry;
pub mod drivers;