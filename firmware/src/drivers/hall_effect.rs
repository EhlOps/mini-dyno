//! Hall-effect RPM sensor driver.
//!
//! A magnet passing the sensor pulls the input low once per magnet; with
//! `divider` magnets evenly spaced around the rotor we see `divider` pulses per
//! revolution. We count rising edges in a GPIO interrupt and convert the count
//! accumulated over a fixed window into an RPM figure.
//!
//! The pin is configured with no internal pull (`InputConfig::default()`): the
//! sensor's open-drain output relies on an external pull-up to idle high, so a
//! passing magnet drives the line low and the trailing rising edge — the magnet
//! leaving — is what we count.
//!
//! esp-hal 1.1 exposes no hardware glitch filter for the C3's GPIOs, so we
//! debounce in software: any edge arriving sooner than `MIN_PULSE_INTERVAL_US`
//! after the previous accepted one is treated as switch bounce / noise and
//! dropped.

use core::cell::RefCell;

use critical_section::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;
use embassy_time::{Duration, Instant, Ticker};
use esp_hal::gpio::{Event, Input, InputConfig, InputPin, Io};
use esp_hal::handler;

/// One-way channel carrying the latest RPM figure from the read loop to any
/// consumer. Interior-mutable, so it can be shared as `&'static`.
pub type RPMSignal = Signal<CriticalSectionRawMutex, u32>;

/// Sampling window. Pulses are counted over this interval, then converted to
/// RPM. Shorter windows react faster but quantize harder at low RPM (fewer
/// pulses per window); 50 ms matches the telemetry cadence.
const WINDOW_MS: u16 = 50;

/// Software glitch filter: edges closer together than this are bounce/noise and
/// are ignored. Set it just below the shortest real pulse spacing at your max
/// RPM. e.g. 4 magnets @ 12000 RPM => 800 pulses/s => 1250 µs spacing, so
/// 300 µs leaves comfortable margin without dropping legitimate pulses.
const MIN_PULSE_INTERVAL_US: u16 = 300;

/// State shared between the GPIO interrupt handler and the sampler. The handler
/// is registered globally (per-pin handlers don't exist), so we stash the pin
/// here to clear its interrupt flag, the last accepted edge time for
/// debouncing, and the running pulse count.
///
/// The count lives here rather than in an atomic because the C3 (rv32imc) has
/// no native atomic read-modify-write; the critical section guards it instead.
struct IsrState {
    pin: Input<'static>,
    last_edge: Instant,
    pulses: u32,
}

static STATE: Mutex<RefCell<Option<IsrState>>> = Mutex::new(RefCell::new(None));

/// Global GPIO interrupt handler. Counts debounced rising edges.
#[handler]
fn hall_isr() {
    critical_section::with(|cs| {
        let mut guard = STATE.borrow_ref_mut(cs);
        let Some(state) = guard.as_mut() else { return };
        if !state.pin.is_interrupt_set() {
            return;
        }
        state.pin.clear_interrupt(); // re-arm for the next edge

        let now = Instant::now();
        if (now - state.last_edge).as_micros() >= MIN_PULSE_INTERVAL_US as u64 {
            state.last_edge = now;
            state.pulses = state.pulses.saturating_add(1);
        }
    });
}

pub struct HallEffectSensor {
    /// Pulses (magnets) per revolution.
    divider: u32,
    /// Drives the fixed sampling cadence in [`read_signal`](Self::read_signal).
    ticker: Ticker,
}

impl HallEffectSensor {
    /// Wire the pin to the GPIO interrupt and arm rising-edge detection.
    ///
    /// `io` is the [`Io`] driver from `main`; the interrupt handler is global,
    /// so this must be called once.
    pub fn new(io: &mut Io<'_>, data: impl InputPin + 'static, divider: u32) -> Self {
        let mut pin = Input::new(data, InputConfig::default());

        io.set_interrupt_handler(hall_isr);
        critical_section::with(|cs| {
            pin.listen(Event::RisingEdge);
            STATE.borrow_ref_mut(cs).replace(IsrState {
                pin,
                last_edge: Instant::now(),
                pulses: 0,
            });
        });

        Self {
            divider,
            ticker: Ticker::every(Duration::from_millis(WINDOW_MS as u64)),
        }
    }

    /// Await one sampling window, then return the RPM measured over it.
    ///
    /// Drains the pulse counter atomically (read-and-reset in one op, so edges
    /// landing between the read and the reset are never lost) and converts the
    /// count to RPM:
    ///
    /// `rpm = (pulses / divider) / (window_s) * 60`
    pub async fn read_signal(&mut self) -> u32 {
        self.ticker.next().await;

        // Read-and-reset the count accumulated over this window in one critical
        // section, so edges landing mid-drain are counted against the next one.
        let pulses = critical_section::with(|cs| {
            STATE
                .borrow_ref_mut(cs)
                .as_mut()
                .map_or(0, |state| core::mem::replace(&mut state.pulses, 0))
        });

        let rpm = (pulses as u64 * 60_000) / (self.divider as u64 * WINDOW_MS as u64);
        rpm as u32
    }
}
