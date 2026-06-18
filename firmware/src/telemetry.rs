//! Dyno telemetry: the data the device measures and publishes.
//!
//! This module owns the [`Telemetry`] sample type, its on-the-wire JSON
//! encoding, and the producer task that feeds the MQTT broker. The broker
//! itself ([`crate::net::mqtt`]) is content-agnostic — it ships whatever bytes
//! land on its [`Feed`]; everything torque/RPM-specific lives here.

use crate::drivers::hall_effect::RPMSignal;
use crate::drivers::hx711::LoadCellSignal;
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
        let _ = write!(
            payload,
            "{{\"torque\":{:.2},\"rpm\":{:.0}}}",
            self.torque, self.rpm
        );
        payload
    }
}

/// Spawns the telemetry producer, pushing samples onto `feed`.
pub fn start(
    spawner: Spawner,
    feed: &'static Feed,
    load_cell: &'static LoadCellSignal,
    rpm: &'static RPMSignal,
) {
    spawner.spawn(producer(feed, load_cell, rpm).unwrap());
}

/// Feeds the broker with telemetry samples at [`SAMPLE_PERIOD`].
///
/// Paced by fresh load-cell samples; the hall-effect RPM signal updates on its
/// own (slower) cadence, so we latch its latest value non-blockingly and reuse
/// it between updates.
#[embassy_executor::task]
async fn producer(
    feed: &'static Feed,
    load_cell: &'static LoadCellSignal,
    rpm: &'static RPMSignal,
) {
    let sender = feed.sender();
    let mut last_rpm: u32 = 0;
    loop {
        let torque = load_cell.wait().await as f32; // Placeholder: interpret the raw HX711 sample as torque.

        // RPM arrives slower than load-cell samples; take the latest if there
        // is one, otherwise carry the previous reading forward.
        if let Some(fresh) = rpm.try_take() {
            last_rpm = fresh;
        }

        let sample = Telemetry {
            torque,
            rpm: last_rpm as f32,
        };
        sender.send(sample.to_payload());

        Timer::after(SAMPLE_PERIOD).await;
    }
}
