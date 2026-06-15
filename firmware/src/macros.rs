//! Small helper macros shared across the firmware.

/// Allocates `val` in a `'static` [`StaticCell`](static_cell::StaticCell) and
/// returns a `&'static mut` reference to it.
///
/// Embassy tasks and `embassy-net` require many of their inputs to be
/// `'static` (the stack resources, the [`Feed`](crate::net::mqtt::Feed), etc.),
/// but those values are computed at runtime in `main`. This macro parks each
/// value in a distinct `static` slot so the reference outlives the current
/// scope without resorting to leaking or global `static mut`.
///
/// Each expansion creates its own `StaticCell`, so a given call site can only
/// be reached once; calling it twice panics (the cell is already initialized).
///
/// ```ignore
/// let feed = mk_static!(Feed, Feed::new());
/// ```
#[macro_export]
macro_rules! mk_static {
    ($t:ty, $val:expr) => {{
        static STATIC_CELL: static_cell::StaticCell<$t> = static_cell::StaticCell::new();
        STATIC_CELL.init_with(|| $val)
    }};
}
