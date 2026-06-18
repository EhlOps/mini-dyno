//! A minimal, payload-agnostic MQTT broker (v3.1.1 and v5.0).
//!
//! The protocol level in each client's CONNECT decides how that connection's
//! CONNACK, SUBACK, and PUBLISH frames are built: v5 carries a property-length
//! byte that v3.1.1 omits. Mixing the two corrupts a v3.1.1 client (a stray
//! `0x00` is parsed as an extra SUBACK granted-code or prepended to the
//! payload), so framing is chosen per connection rather than hardcoded.
//!
//! The device is the *only* publisher: some producer task pushes the latest
//! [`Payload`] onto a [`Feed`], and this broker forwards it to every connected
//! subscriber. Clients only ever subscribe, so the broker implements just the
//! slice of MQTT 5 that requires:
//!
//! - `CONNECT`  → reply `CONNACK`
//! - `SUBSCRIBE` → reply `SUBACK`, then start forwarding the feed
//! - `PINGREQ`  → reply `PINGRESP`
//! - `DISCONNECT` → close the connection
//! - outbound `PUBLISH` (QoS 0) carrying the latest [`Payload`]
//!
//! There is no inter-client routing, no retained-message store, and no QoS 1/2
//! — none of which a one-way feed needs.
//!
//! The broker is deliberately content-agnostic: it ships whatever bytes appear
//! on the [`Feed`] to whatever [`Config::topic`] it is configured with. The
//! meaning of those bytes (e.g. JSON telemetry) lives in the producer module,
//! not here. Type erasure at the [`Feed`] boundary is also what lets the
//! connection task stay concrete — `#[embassy_executor::task]` functions cannot
//! be generic.
//!
//! ## Concurrency model
//!
//! The feed is a single-producer / multi-consumer [`Watch`] holding the latest
//! [`Payload`]. The producer calls [`Feed::sender`] and pushes; each connection
//! task holds a receiver and forwards changes. `Watch` keeps only the most
//! recent value, so a slow client drops intermediate samples instead of
//! stalling the producer.
//!
//! [`start`] spawns [`MAX_CLIENTS`] connection tasks up front. Each owns its own
//! heap-allocated socket buffers and loops on `accept`, so the pool transparently
//! handles clients reconnecting. `embassy-net` permits several sockets to listen
//! on the same port concurrently.

use alloc::vec;
use defmt::{info, warn};
use embassy_executor::Spawner;
use embassy_futures::select::{Either, select};
use embassy_net::{Stack, tcp::TcpSocket};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::watch::Watch;
use embassy_time::Duration;
use embedded_io_async::{Read, Write};

/// Maximum number of simultaneously connected MQTT clients.
///
/// This bounds both the connection-task pool and the number of [`Feed`]
/// receivers, and feeds `SOCKETS` (in the parent module) so the network stack has a
/// TCP slot for each client.
pub const MAX_CLIENTS: usize = 4;

/// Maximum size in bytes of a single published payload.
pub const PAYLOAD_CAP: usize = 128;

/// Size of the scratch buffer used to encode an outbound PUBLISH frame:
/// fixed header (≤5) + topic length (2) + topic + property length (1) + payload.
const TX_FRAME: usize = PAYLOAD_CAP + 64;

// MQTT control packet types (the high nibble of the fixed-header first byte).
const CONNECT: u8 = 1;
const SUBSCRIBE: u8 = 8;
const PINGREQ: u8 = 12;
const DISCONNECT: u8 = 14;

// PINGRESP is identical across MQTT versions. CONNACK and SUBACK are not —
// v5 frames carry an extra property-length byte that v3.1.1 must never see, so
// those are built per-connection from the negotiated protocol level instead.
const PINGRESP: [u8; 2] = [0xD0, 0x00];

/// Upper bound on the number of granted QoS codes echoed in a SUBACK.
///
/// A SUBACK must return exactly one reason code per topic filter in the
/// SUBSCRIBE; returning more crashes some clients (mqtt.js writes the codes
/// back onto the subscription array and indexes past its end). Filter counts
/// above this are clamped down, which only ever *under*-reports — never the
/// over-report that triggers the crash.
const MAX_GRANTED: usize = 16;

/// Broker configuration: where to listen and what topic to publish under.
#[derive(Clone, Copy)]
pub struct Config {
    /// TCP port to listen on (1883 is the IANA-assigned MQTT port).
    pub port: u16,
    /// Topic every outbound PUBLISH is tagged with. Subscribers should
    /// subscribe here (or to a wildcard that matches it — the broker forwards
    /// to anyone who has sent a `SUBSCRIBE`, since there is only one topic).
    pub topic: &'static str,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            port: 1883,
            topic: "",
        }
    }
}

/// A fixed-capacity publish payload.
///
/// Build one with [`Payload::from_bytes`], or by formatting into it directly —
/// it implements [`core::fmt::Write`], so producers can `write!` text (e.g.
/// JSON) straight into the buffer.
#[derive(Clone, Copy)]
pub struct Payload {
    buf: [u8; PAYLOAD_CAP],
    len: usize,
}

impl Payload {
    /// Creates an empty payload.
    pub const fn new() -> Self {
        Self {
            buf: [0u8; PAYLOAD_CAP],
            len: 0,
        }
    }

    /// Creates a payload from raw bytes, or `None` if they exceed [`PAYLOAD_CAP`].
    pub fn from_bytes(bytes: &[u8]) -> Option<Self> {
        let mut p = Self::new();
        p.buf.get_mut(..bytes.len())?.copy_from_slice(bytes);
        p.len = bytes.len();
        Some(p)
    }

    /// Returns the payload bytes.
    pub fn as_slice(&self) -> &[u8] {
        &self.buf[..self.len]
    }
}

impl Default for Payload {
    fn default() -> Self {
        Self::new()
    }
}

impl core::fmt::Write for Payload {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        let bytes = s.as_bytes();
        let end = self.len + bytes.len();
        if end > PAYLOAD_CAP {
            return Err(core::fmt::Error);
        }
        self.buf[self.len..end].copy_from_slice(bytes);
        self.len = end;
        Ok(())
    }
}

/// Single-producer, multi-consumer channel carrying the latest [`Payload`].
///
/// Create one (typically via `mk_static!`), hand it to [`start`] for the
/// subscriber side, and call [`Feed::sender`] on the producer side.
pub type Feed = Watch<CriticalSectionRawMutex, Payload, MAX_CLIENTS>;

/// Spawns the broker's connection-task pool.
///
/// Launches [`MAX_CLIENTS`] independent tasks, each listening on `config.port`
/// and forwarding the `feed` to whichever client it accepts. Call once after
/// the network [`Stack`] is up.
pub fn start(spawner: Spawner, stack: Stack<'static>, feed: &'static Feed, config: Config) {
    for _ in 0..MAX_CLIENTS {
        // pool_size on the task matches MAX_CLIENTS, so all spawns succeed.
        spawner.spawn(connection_task(stack, feed, config).unwrap());
    }
}

/// Serves a single MQTT client for the lifetime of the device.
///
/// Grabs one [`Feed`] receiver, then loops forever: accept a connection, run
/// the session until the client disconnects or errors, tear the socket down,
/// and go back to accepting. Socket buffers are heap-allocated once and reused
/// across reconnects.
#[embassy_executor::task(pool_size = MAX_CLIENTS)]
async fn connection_task(stack: Stack<'static>, feed: &'static Feed, config: Config) {
    let mut receiver = feed
        .receiver()
        .expect("more connection tasks than Feed receivers");

    let mut rx = vec![0u8; 1024];
    let mut tx = vec![0u8; 1024];
    let mut buf = vec![0u8; 1024];

    loop {
        let mut socket = TcpSocket::new(stack, &mut rx, &mut tx);
        // A client can leave the Wi-Fi AP without closing its TCP connection
        // (an abrupt `AssociationLeave`, no FIN). The keep-alive probes the peer
        // during inactivity and the timeout — set longer than the keep-alive —
        // reaps the half-open socket if it stops answering, so a departed client
        // is detected in ~10 s instead of lingering and forcing smoltcp to
        // retransmit telemetry into the void (each attempt logs a radio
        // `NOT_ASSOC` warning and ties up a connection slot).
        socket.set_keep_alive(Some(Duration::from_secs(5)));
        socket.set_timeout(Some(Duration::from_secs(10)));

        info!("MQTT: listening on :{}", config.port);
        if let Err(e) = socket.accept(config.port).await {
            warn!("MQTT: accept error: {:?}", e);
            continue;
        }
        info!("MQTT: client connected");

        // Split so we can wait on "client sent us a packet" and "new payload
        // available" concurrently — the reader and writer borrow the socket
        // independently. The labeled block lets any error bail straight to the
        // teardown below.
        {
            let (mut reader, mut writer) = socket.split();
            'session: {
                // Handshake: the first packet must be CONNECT. Its variable
                // header is `protocol-name-len(2) + "MQTT"(4) + level(1) + ...`,
                // so byte 6 is the protocol level: 4 = v3.1.1, 5 = v5. That
                // choice frames every reply below — v5 adds a property-length
                // byte to CONNACK/SUBACK/PUBLISH that v3.1.1 must not receive.
                let v5 = match read_packet(&mut reader, &mut buf).await {
                    Ok((CONNECT, len)) if len >= 7 => buf[6] >= 5,
                    _ => break 'session,
                };
                let connack: &[u8] = if v5 {
                    &[0x20, 0x03, 0x00, 0x00, 0x00] // session-present 0, reason 0, props 0
                } else {
                    &[0x20, 0x02, 0x00, 0x00] // session-present 0, reason 0 (no props field)
                };
                if writer.write_all(connack).await.is_err() {
                    break 'session;
                }
                let _ = writer.flush().await;

                let mut subscribed = false;
                loop {
                    match select(read_packet(&mut reader, &mut buf), receiver.changed()).await {
                        // A packet arrived from the client.
                        Either::First(Ok((typ, len))) => match typ {
                            // The SUBSCRIBE variable header starts with the
                            // 2-byte packet identifier (echoed in SUBACK),
                            // followed by one reason code per requested filter.
                            SUBSCRIBE if len >= 2 => {
                                let granted = count_filters(v5, &buf[..len])
                                    .unwrap_or(1)
                                    .clamp(1, MAX_GRANTED);
                                let mut suback = [0u8; 5 + MAX_GRANTED];
                                let mut pos = 0;
                                let _ = write_byte(&mut suback, &mut pos, 0x90); // SUBACK
                                // remaining = packet id (2) + v5 property length
                                // (1) + one granted byte per filter.
                                let remaining = 2 + usize::from(v5) + granted;
                                pos += encode_varint(remaining as u32, &mut suback[pos..])
                                    .unwrap_or(0);
                                let _ = write_byte(&mut suback, &mut pos, buf[0]);
                                let _ = write_byte(&mut suback, &mut pos, buf[1]);
                                if v5 {
                                    let _ = write_byte(&mut suback, &mut pos, 0x00); // property length
                                }
                                for _ in 0..granted {
                                    let _ = write_byte(&mut suback, &mut pos, 0x00); // QoS 0 granted
                                }
                                if writer.write_all(&suback[..pos]).await.is_err() {
                                    break 'session;
                                }
                                let _ = writer.flush().await;
                                subscribed = true;
                                info!("MQTT: client subscribed");
                            }
                            PINGREQ => {
                                if writer.write_all(&PINGRESP).await.is_err() {
                                    break 'session;
                                }
                                let _ = writer.flush().await;
                            }
                            DISCONNECT => break 'session,
                            // Clients shouldn't publish here; ignore anything else.
                            _ => {}
                        },
                        // Read error or peer closed the connection.
                        Either::First(Err(())) => break 'session,
                        // Fresh payload — forward it if the client subscribed.
                        Either::Second(payload) => {
                            if subscribed {
                                let mut out = [0u8; TX_FRAME];
                                if let Some(n) =
                                    encode_publish(&mut out, config.topic, payload.as_slice(), v5)
                                {
                                    if writer.write_all(&out[..n]).await.is_err() {
                                        break 'session;
                                    }
                                    let _ = writer.flush().await;
                                }
                            }
                        }
                    }
                }
            }
        }

        socket.close();
        let _ = socket.flush().await;
        info!("MQTT: client disconnected");
    }
}

/// Reads one full MQTT control packet.
///
/// Returns `(packet_type, body_len)` where `packet_type` is the high nibble of
/// the fixed-header first byte and `buf[..body_len]` holds the variable header
/// plus payload. Returns `Err` if the peer closes, on any I/O error, or if the
/// packet's remaining length exceeds `buf`.
async fn read_packet<R: Read>(reader: &mut R, buf: &mut [u8]) -> Result<(u8, usize), ()> {
    let mut hdr = [0u8; 1];
    read_exact(reader, &mut hdr).await?;
    let typ = hdr[0] >> 4;

    // "Remaining Length" is a variable-length integer: 7 bits per byte, with
    // the top bit set on every byte except the last (max 4 bytes).
    let mut len: u32 = 0;
    let mut multiplier: u32 = 1;
    loop {
        let mut b = [0u8; 1];
        read_exact(reader, &mut b).await?;
        len += u32::from(b[0] & 0x7F) * multiplier;
        if b[0] & 0x80 == 0 {
            break;
        }
        multiplier = multiplier.checked_mul(128).ok_or(())?;
        if multiplier > 128 * 128 * 128 {
            return Err(()); // malformed: more than 4 length bytes
        }
    }

    let len = len as usize;
    if len > buf.len() {
        return Err(());
    }
    read_exact(reader, &mut buf[..len]).await?;
    Ok((typ, len))
}

/// Reads exactly `buf.len()` bytes, treating a clean EOF as an error.
async fn read_exact<R: Read>(reader: &mut R, buf: &mut [u8]) -> Result<(), ()> {
    let mut filled = 0;
    while filled < buf.len() {
        match reader.read(&mut buf[filled..]).await {
            Ok(0) => return Err(()), // peer closed
            Ok(n) => filled += n,
            Err(_) => return Err(()),
        }
    }
    Ok(())
}

/// Encodes a QoS-0 `PUBLISH` for `topic`/`payload` into `out`.
///
/// Frames for MQTT v5 when `v5` is set (a zero property-length byte follows the
/// topic) or v3.1.1 otherwise (no property field — emitting one would prepend a
/// stray `0x00` to the payload a v3.1.1 client delivers). Returns the encoded
/// length, or `None` if `out` is too small.
fn encode_publish(out: &mut [u8], topic: &str, payload: &[u8], v5: bool) -> Option<usize> {
    let topic = topic.as_bytes();
    // variable header + payload: topic-length(2) + topic + [v5 property-length(1)] + payload
    let remaining = 2 + topic.len() + usize::from(v5) + payload.len();

    let mut pos = 0;
    write_byte(out, &mut pos, 0x30)?; // PUBLISH, DUP=0, QoS=0, RETAIN=0
    pos += encode_varint(remaining as u32, out.get_mut(pos..)?)?;
    write_byte(out, &mut pos, (topic.len() >> 8) as u8)?;
    write_byte(out, &mut pos, (topic.len() & 0xFF) as u8)?;
    write_slice(out, &mut pos, topic)?;
    if v5 {
        write_byte(out, &mut pos, 0x00)?; // property length = 0 (no properties)
    }
    write_slice(out, &mut pos, payload)?;
    Some(pos)
}

/// Encodes `value` as an MQTT variable-length integer into `out`, returning the
/// number of bytes written, or `None` if `out` is too small.
fn encode_varint(mut value: u32, out: &mut [u8]) -> Option<usize> {
    let mut i = 0;
    loop {
        let mut byte = (value % 128) as u8;
        value /= 128;
        if value > 0 {
            byte |= 0x80;
        }
        *out.get_mut(i)? = byte;
        i += 1;
        if value == 0 {
            return Some(i);
        }
    }
}

/// Counts the topic filters in a SUBSCRIBE body so the SUBACK can return one
/// reason code per filter.
///
/// `body` is the variable header + payload (no fixed header). Layout: a 2-byte
/// packet identifier, then — for v5 only — a property block (varint length plus
/// that many bytes), then a sequence of `filter-length(2) + filter + options(1)`
/// entries. Returns `None` on a body too short or malformed to walk.
fn count_filters(v5: bool, body: &[u8]) -> Option<usize> {
    let mut pos = 2; // skip the packet identifier
    if v5 {
        let (props, used) = read_varint(body.get(pos..)?)?;
        pos += used + props as usize;
    }
    let mut count = 0;
    while pos < body.len() {
        let hi = usize::from(*body.get(pos)?);
        let lo = usize::from(*body.get(pos + 1)?);
        pos += 2 + ((hi << 8) | lo) + 1; // filter length + filter + options byte
        count += 1;
    }
    Some(count)
}

/// Decodes an MQTT variable-length integer from the front of `buf`, returning
/// `(value, bytes_consumed)`, or `None` if `buf` ends early or the encoding runs
/// past the 4-byte maximum.
fn read_varint(buf: &[u8]) -> Option<(u32, usize)> {
    let mut value = 0u32;
    let mut multiplier = 1u32;
    let mut i = 0;
    loop {
        let b = *buf.get(i)?;
        value += u32::from(b & 0x7F) * multiplier;
        i += 1;
        if b & 0x80 == 0 {
            return Some((value, i));
        }
        if i >= 4 {
            return None;
        }
        multiplier *= 128;
    }
}

fn write_byte(out: &mut [u8], pos: &mut usize, b: u8) -> Option<()> {
    *out.get_mut(*pos)? = b;
    *pos += 1;
    Some(())
}

fn write_slice(out: &mut [u8], pos: &mut usize, src: &[u8]) -> Option<()> {
    out.get_mut(*pos..*pos + src.len())?.copy_from_slice(src);
    *pos += src.len();
    Some(())
}
