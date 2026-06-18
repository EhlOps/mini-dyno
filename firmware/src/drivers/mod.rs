//! Sensor drivers and the tasks that pump them.
//!
//! Each sensor is a self-contained driver — [`hx711`] for the load cell,
//! [`hall_effect`] for the RPM pickup — paired here with an embassy task that
//! reads it in a tight loop and republishes the latest value on a [`Signal`].
//!
//! Splitting acquisition (these loops) from consumption ([`crate::telemetry`])
//! through a `Signal` decouples their cadences: each sensor samples as fast as
//! its hardware allows, the latest reading overwrites the previous one, and the
//! telemetry producer latches whatever is current without ever blocking a
//! driver. `main` spawns one loop per sensor after wiring up the GPIOs.
//!
//! [`Signal`]: embassy_sync::signal::Signal

use hx711::{HX711, LoadCellSignal};
use hall_effect::{HallEffectSensor, RPMSignal};

pub mod hx711;
pub mod hall_effect;

/// Continuously reads the HX711 load cell and publishes each raw sample on
/// `signal`, overwriting any previous unread value.
#[embassy_executor::task]
pub async fn hx711_read_loop(hx711: &'static mut HX711, signal: &'static LoadCellSignal) {
    loop {
        let sample = hx711.read_sample().await;
        signal.signal(sample);
    }
}

/// Continuously samples the hall-effect sensor (one value per sampling window)
/// and publishes the latest RPM figure on `signal`.
#[embassy_executor::task]
pub async fn hall_effect_read_loop(sensor: &'static mut HallEffectSensor, signal: &'static RPMSignal) {
    loop {
        let rpm = sensor.read_signal().await;
        signal.signal(rpm);
    }
}

