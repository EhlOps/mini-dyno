//! Background tasks that drive the networking stack.
//!
//! These are spawned by [`Net::run`](super::Net::run) and run for the lifetime
//! of the device:
//! - [`net_task`] polls the `embassy-net` stack.
//! - [`connection`] logs client association events on the access point.
//! - [`run_dhcp`] hands out leases so clients can obtain an IP address.

use core::{net::Ipv4Addr, str::FromStr};
use defmt::info;
use edge_dhcp::io::DEFAULT_SERVER_PORT;
use edge_dhcp::{
    Options, Packet,
    server::{Server, ServerOptions},
};
use embassy_net::Runner;
use embassy_net::{
    IpAddress, IpEndpoint, Stack,
    udp::{PacketMetadata, UdpSocket},
};
use embassy_time::{Duration, Timer};
use esp_radio::wifi::{Interface, WifiController};
use esp_radio::wifi::ap::EventInfo;

/// Runs a minimal DHCP server so clients joining the access point can lease an
/// address.
///
/// Binds a UDP socket on the standard DHCP server port and serves requests in a
/// loop, advertising `ip_addr_str` as both the server identity and the gateway.
/// Replies are sent unicast when the client already has an address and did not
/// request broadcast, and to `255.255.255.255` otherwise — matching the
/// behavior of `edge_dhcp`'s own run loop, which this inlines onto an
/// `embassy-net` socket. Malformed or undeliverable packets are logged and
/// skipped; the task never returns.
///
/// # Panics
/// - if `ip_addr_str` is not a valid IPv4 address.
/// - if binding the DHCP server port fails.
#[embassy_executor::task]
pub async fn run_dhcp(stack: Stack<'static>, ip_addr_str: &'static str) {
    let ip = Ipv4Addr::from_str(ip_addr_str).expect("Invalid IP address format");

    // embassy-net socket buffers (replaces edge_nal_embassy's UdpBuffers).
    let mut rx_meta = [PacketMetadata::EMPTY; 10];
    let mut rx_buf = [0u8; 1500];
    let mut tx_meta = [PacketMetadata::EMPTY; 10];
    let mut tx_buf = [0u8; 1500];
    let mut buf = [0u8; 1500];

    let mut gw_buf = [Ipv4Addr::UNSPECIFIED];

    let mut socket = UdpSocket::new(stack, &mut rx_meta, &mut rx_buf, &mut tx_meta, &mut tx_buf);
    socket.bind(DEFAULT_SERVER_PORT).expect("DHCP bind failed");

    let mut server = Server::<_, 64>::new_with_et(ip);
    let server_options = ServerOptions::new(ip, Some(&mut gw_buf));

    // Inlined equivalent of edge_dhcp::io::server::run, on embassy-net's UdpSocket.
    loop {
        let (len, meta) = match socket.recv_from(&mut buf).await {
            Ok(v) => v,
            Err(e) => {
                defmt::warn!("DHCP recv error: {:?}", e);
                continue;
            }
        };

        let request = match Packet::decode(&buf[..len]) {
            Ok(r) => r,
            Err(e) => {
                defmt::warn!("DHCP decode error: {:?}", defmt::Debug2Format(&e));
                continue;
            }
        };

        let mut opt_buf = Options::buf();

        if let Some(reply) = server.handle_request(&mut opt_buf, &server_options, &request) {
            // `yiaddr` ("your IP address") is the lease the server is offering /
            // acknowledging; it's UNSPECIFIED for NAKs and other replies.
            if !reply.yiaddr.is_unspecified() {
                info!("DHCP assigned {} to {:?}", reply.yiaddr, request.chaddr);
            }

            // Same broadcast-vs-unicast choice the library makes: if the client
            // set the broadcast flag or has no address yet, reply to 255.255.255.255.
            let dst = match meta.endpoint.addr {
                IpAddress::Ipv4(client_ip) if !request.broadcast && !client_ip.is_unspecified() => {
                    meta.endpoint
                }
                _ => IpEndpoint::new(IpAddress::Ipv4(Ipv4Addr::BROADCAST), meta.endpoint.port),
            };

            let encoded = match reply.encode(&mut buf) {
                Ok(r) => r,
                Err(e) => {
                    defmt::warn!("DHCP encode error: {:?}", defmt::Debug2Format(&e));
                    continue;
                }
            };

            if let Err(e) = socket.send_to(encoded, dst).await {
                defmt::warn!("DHCP send error: {:?}", e);
            }
        }
    }
}

/// Logs client association events on the access point.
///
/// Awaits connect/disconnect events from the Wi-Fi controller and logs them,
/// throttling each iteration by 5 seconds. Purely observational — it performs
/// no connection management — and never returns.
#[embassy_executor::task]
pub async fn connection(controller: WifiController<'static>) {
    info!("Start Connection Task");
    loop {
        let ev = controller
            .wait_for_access_point_connected_event_async()
            .await;
        match ev {
            Ok(EventInfo::Connected(info)) => {
                info!("Client connected: {:?}", info);
            }
            Ok(EventInfo::Disconnected(info)) => {
                info!("Client disconnected: {:?}", info);
            }
            _ => ()
        }
        Timer::after(Duration::from_millis(5000)).await
    }
}

/// Drives the `embassy-net` stack.
///
/// Hands control to the stack's [`Runner`], which processes the interface and
/// services sockets for as long as the device is powered. Never returns.
#[embassy_executor::task]
pub async fn net_task(mut runner: Runner<'static, Interface>) {
    runner.run().await
}