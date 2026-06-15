#![no_std]
#![no_main]
#![deny(
    clippy::mem_forget,
    reason = "mem::forget is generally not safe to do with esp_hal types, especially those \
    holding buffers for the duration of a data transfer."
)]
#![deny(clippy::large_stack_frames)]

//! Binary entry point for the mini-dyno firmware.
//!
//! Boots the ESP32-C3: configures clocks and heap, starts the `esp-rtos`
//! executor, then brings up the network and telemetry pipeline defined in the
//! [`firmware`] library crate:
//!
//! 1. [`Net::new`](firmware::net::Net::new) opens the `mini-dyno` Wi-Fi access
//!    point and a static IPv4 stack at `192.168.1.1`.
//! 2. [`mqtt::start`](firmware::net::mqtt::start) launches the MQTT broker on
//!    TCP `1883`, publishing under [`telemetry::TOPIC`](firmware::telemetry::TOPIC).
//! 3. [`telemetry::start`](firmware::telemetry::start) spawns the producer that
//!    pushes torque/RPM samples onto the shared [`Feed`].
//!
//! After setup `main` idles forever; all real work runs in the spawned tasks.

use defmt::{error, info};
use embassy_executor::Spawner;
use embassy_time::{Duration, Timer};
use esp_hal::clock::CpuClock;
use esp_hal::timer::timg::TimerGroup;
use esp_println as _;
use firmware::mk_static;
use firmware::net::Net;
use firmware::net::mqtt::{self, Feed};
use firmware::telemetry;

#[panic_handler]
fn panic(panic_info: &core::panic::PanicInfo) -> ! {
    error!("{}", panic_info);
    loop {}
}

extern crate alloc;

// This creates a default app-descriptor required by the esp-idf bootloader.
// For more information see: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/app_image_format.html#application-description>
esp_bootloader_esp_idf::esp_app_desc!();

#[allow(
    clippy::large_stack_frames,
    reason = "it's not unusual to allocate larger buffers etc. in main"
)]
#[esp_rtos::main]
async fn main(spawner: Spawner) -> ! {
    // generator version: 1.3.0
    // generator parameters: --chip esp32c3 -o esp32c3-mini-1 -o unstable-hal -o alloc -o wifi -o embassy -o defmt -o wokwi -o vscode

    let config = esp_hal::Config::default().with_cpu_clock(CpuClock::max());
    let peripherals = esp_hal::init(config);

    esp_alloc::heap_allocator!(#[esp_hal::ram(reclaimed)] size: 64 * 1024);
    esp_alloc::heap_allocator!(size: 36 * 1024);

    let timg0 = TimerGroup::new(peripherals.TIMG0);
    let sw_interrupt =
        esp_hal::interrupt::software::SoftwareInterruptControl::new(peripherals.SW_INTERRUPT);
    esp_rtos::start(timg0.timer0, sw_interrupt.software_interrupt0);

    info!("Embassy initialized!");

    let net = Net::new(peripherals.WIFI, "mini-dyno", "192.168.1.1");
    let stack = net.run(spawner).await;

    // Bring up the MQTT 5 broker and the telemetry feed it serves. Clients
    // connect to tcp 192.168.1.1:1883 and subscribe to "dyno/telemetry".
    let feed = mk_static!(Feed, Feed::new());
    mqtt::start(
        spawner,
        stack,
        feed,
        mqtt::Config {
            port: 1883,
            topic: telemetry::TOPIC,
        },
    );
    telemetry::start(spawner, feed);

    loop {
        Timer::after(Duration::from_secs(1)).await;
    }
}
