//! HX711 24-bit load-cell ADC driver.
//!
//! The HX711 has no addressable bus — it speaks a tiny two-wire protocol that
//! we bit-bang directly: a clock line we drive (`PD_SCK`) and a data line it
//! drives (`DOUT`). The chip pulls `DOUT` low when a fresh conversion is ready;
//! we then clock out 24 bits MSB-first, one bit per clock pulse, and finish
//! with extra pulses that select the gain/channel for the *next* conversion.
//!
//! A single trailing pulse (25 total) selects channel A at gain 128, which is
//! what this driver uses. The conversion itself is signed 24-bit two's
//! complement; we return the raw 24-bit word unmodified — interpreting it as a
//! force/torque figure (offset, scale, sign) is left to the consumer. Today
//! [`crate::telemetry`] uses it as a placeholder torque value pending
//! calibration.
//!
//! The protocol is timing-sensitive but slow, so we satisfy the pulse-width
//! requirement with `embassy-time` delays and keep the whole read `async` — the
//! `while data is high` wait yields the executor instead of busy-spinning.

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;
use embassy_time::{Duration, Timer};
use esp_hal::gpio::{Input, InputConfig, InputPin, Level, Output, OutputConfig, OutputPin};

/// One-way channel carrying the latest raw HX711 sample from the read loop to
/// any consumer. Interior-mutable, so it can be shared as `&'static`.
pub type LoadCellSignal = Signal<CriticalSectionRawMutex, u32>;

/// A single HX711 connection: the clock line we drive and the data line it
/// drives. Owns both GPIOs for the lifetime of the driver.
pub struct HX711 {
    data_pin: Input<'static>,
    clock_pin: Output<'static>,
}

impl HX711 {
    /// Bind the two protocol pins. `clock` is `PD_SCK` (output, idles low) and
    /// `data` is `DOUT` (input). The chip is left powered up; holding `clock`
    /// high for >60 µs would power it down, which this driver never does.
    pub fn new(clock: impl OutputPin + 'static, data: impl InputPin + 'static) -> Self {
        let clock_pin = Output::new(clock, Level::Low, OutputConfig::default());
        let data_pin = Input::new(data, InputConfig::default());

        Self {
            data_pin,
            clock_pin,
        }
    }

    /// Emit one clock pulse: drive `PD_SCK` high, hold, drop it low, hold.
    ///
    /// Each rising edge clocks out the next bit (or, for the trailing pulses,
    /// programs the gain/channel). The 1 µs holds clear the chip's minimum
    /// high/low pulse-width spec with margin.
    pub async fn pulse_clock(&mut self) {
        self.clock_pin.set_high();
        Timer::after(Duration::from_micros(1)).await; // Short delay to meet timing requirements.
        self.clock_pin.set_low();
        Timer::after(Duration::from_micros(1)).await; // Short delay before the next clock pulse.
    }

    /// Wait for a conversion, then clock out and return the raw 24-bit reading.
    ///
    /// Blocks (yielding the executor) until `DOUT` goes low to signal data
    /// ready, shifts in 24 bits MSB-first, then emits one extra pulse to select
    /// channel A / gain 128 for the next conversion. The returned `u32` is the
    /// raw 24-bit word (signed two's complement on the wire) with no offset,
    /// scaling, or sign extension applied.
    pub async fn read_sample(&mut self) -> u32 {
        // Wait for data pin to go low, indicating a new sample is ready.
        while self.data_pin.is_high() {
            Timer::after(Duration::from_millis(1)).await;
        }

        // Read 24 bits of data, MSB first.
        let mut value: u32 = 0;
        for _ in 0..24 {
            self.pulse_clock().await; // Pulse the clock pin to signal the HX711 to output the next bit.
            value <<= 1; // Shift existing bits left to make room for the next bit.

            // Read the current bit from the data pin and set it in `value`.
            if self.data_pin.is_high() {
                value |= 1; // Set the least significant bit if data pin is high.
            }
        }

        self.pulse_clock().await; // Final pulse to prepare for the next reading.

        value
    }
}
