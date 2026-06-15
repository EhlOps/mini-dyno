//! Networking for the mini-dyno.
//!
//! Brings up a Wi-Fi access point and an [`embassy-net`](embassy_net) IPv4
//! stack so clients can connect to the device over TCP/IP. The [`Net`] type
//! owns the radio controller, the network stack, and the background runner
//! that drives it.

use crate::mk_static;
use core::{net::Ipv4Addr, str::FromStr};
use defmt::info;
use embassy_executor::Spawner;
use embassy_net::{Config as NetConfig, Ipv4Cidr, Runner, Stack, StackResources, StaticConfigV4};
use esp_hal::{peripherals::WIFI, rng::Rng};
use esp_radio::wifi::{Config, ControllerConfig, Interface, WifiController, ap::AccessPointConfig};

pub mod mqtt;
mod tasks;

/// Number of `embassy-net` socket slots: one UDP socket for the DHCP server
/// plus one TCP socket per concurrent MQTT client, with a little headroom.
const SOCKETS: usize = mqtt::MAX_CLIENTS + 2;

/// Owns the Wi-Fi access point and its `embassy-net` networking stack.
///
/// Construct one with [`Net::new`], then hand it to [`Net::run`] to wait for
/// the interface to come up and obtain the shared [`Stack`] used for sockets.
pub struct Net {
    /// Radio controller for the Wi-Fi peripheral.
    controller: WifiController<'static>,
    /// Shared, cloneable handle to the network stack used to open sockets.
    stack: Stack<'static>,
    /// Background task that polls the stack and services the interface.
    runner: Runner<'static, Interface>,
    /// The static IP address assigned to the device, used for logging and as the gateway for clients.
    ip_addr_str: &'static str,
}

impl Net {
    /// Creates an open Wi-Fi access point and a statically-addressed IPv4
    /// stack.
    ///
    /// The access point broadcasts `ssid` with no password. `ip_addr_str` is
    /// assigned to the device as a `/24` static address and also used as the
    /// gateway; clients on the subnet reach the device at this address. A
    /// random seed for the stack is drawn from the hardware [`Rng`].
    ///
    /// The returned [`Net`] is inert until driven by [`Net::run`].
    ///
    /// # Panics
    /// - if `ip_addr_str` is not a valid IPv4 address.
    /// - if the Wi-Fi controller fails to initialize.
    pub fn new(wifi: WIFI<'static>, ssid: &str, ip_addr_str: &'static str) -> Self {
        let config = Config::AccessPoint(AccessPointConfig::default().with_ssid(ssid));
        let device = esp_radio::wifi::Interface::access_point();
        let controller = esp_radio::wifi::WifiController::new(
            wifi,
            ControllerConfig::default().with_initial_config(config),
        )
        .unwrap();

        let ip_addr = Ipv4Addr::from_str(ip_addr_str).expect("Invalid IP address format");
        let net_config = NetConfig::ipv4_static(StaticConfigV4 {
            address: Ipv4Cidr::new(ip_addr, 24),
            gateway: Some(ip_addr),
            dns_servers: Default::default(),
        });

        let rng = Rng::new();
        let seed = (rng.random() as u64) << 32 | (rng.random() as u64);
        let resources = mk_static!(StackResources<SOCKETS>, StackResources::<SOCKETS>::new());
        let (stack, runner) = embassy_net::new(device, net_config, resources, seed);

        Self {
            controller,
            stack,
            runner,
            ip_addr_str,
        }
    }

    /// Waits for the network interface to come up and returns the shared
    /// [`Stack`].
    ///
    /// Blocks until the IPv4 configuration is applied, logs the assigned
    /// address, and yields the [`Stack`] handle for opening sockets. The
    /// returned handle is cheaply cloneable and `'static`.
    pub async fn run(self, spawner: Spawner) -> Stack<'static> {
        spawner.spawn(tasks::connection(self.controller).unwrap());
        spawner.spawn(tasks::net_task(self.runner).unwrap());
        spawner.spawn(tasks::run_dhcp(self.stack, self.ip_addr_str).unwrap());
        self.stack.wait_config_up().await;
        self.stack
            .config_v4()
            .inspect(|ip| info!("Network configured with IP address: {}", ip));
        self.stack
    }
}
