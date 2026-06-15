//! Dyno telemetry: the data the device measures and publishes.
//!
//! This module owns the [`Telemetry`] sample type, its on-the-wire JSON
//! encoding, and the producer task that feeds the MQTT broker. The broker
//! itself ([`crate::net::mqtt`]) is content-agnostic — it ships whatever bytes
//! land on its [`Feed`]; everything torque/RPM-specific lives here.

use crate::net::mqtt::{Feed, Payload};
use core::fmt::Write;
use embassy_executor::Spawner;
use embassy_time::{Duration, Timer};

/// MQTT topic the device publishes telemetry under.
pub const TOPIC: &str = "dyno/telemetry";

/// Sample rate of the producer task.
const SAMPLE_PERIOD: Duration = Duration::from_millis(20); // 50 Hz

/// One dynamometer sample: torque in newton-metres and shaft speed in RPM.
#[derive(Clone, Copy, defmt::Format)]
pub struct Telemetry {
    pub torque: f32,
    pub rpm: f32,
}

impl Telemetry {
    /// Serializes to the JSON the mobile app expects:
    /// `{"torque":<n.nn>,"rpm":<n>}`.
    fn to_payload(self) -> Payload {
        let mut payload = Payload::new();
        // Infallible for this format: it is far shorter than PAYLOAD_CAP.
        let _ = write!(payload, "{{\"torque\":{:.2},\"rpm\":{:.0}}}", self.torque, self.rpm);
        payload
    }
}

/// Spawns the telemetry producer, pushing samples onto `feed`.
pub fn start(spawner: Spawner, feed: &'static Feed) {
    spawner.spawn(producer(feed).unwrap());
}

/// Feeds the broker with telemetry samples at [`SAMPLE_PERIOD`].
///
/// PLACEHOLDER: this currently synthesizes a sweeping torque/RPM curve so the
/// broker can be exercised end-to-end. Replace the body with real HX711
/// load-cell and hall-effect RPM reads once those drivers are ported to Rust.
#[embassy_executor::task]
async fn producer(feed: &'static Feed) {
    let sender = feed.sender();
    let mut tick: u32 = 0;
    loop {
        let phase = (tick % 200) as f32 / 200.0; // 0..1 sweep over ~4 s
        let sample = Telemetry {
            torque: 20.0 + 15.0 * phase,
            rpm: 1000.0 + 7000.0 * phase,
        };
        sender.send(sample.to_payload());

        tick = tick.wrapping_add(1);
        Timer::after(SAMPLE_PERIOD).await;
    }
}
