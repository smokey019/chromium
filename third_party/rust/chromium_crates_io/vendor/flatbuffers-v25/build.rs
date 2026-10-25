use rustc_version::{Channel, version_meta};

fn main() {
    let version_meta = version_meta().unwrap();

    // To use nightly features we declare this and then we can use
    // #[cfg(nightly)]
    // for nightly only features
    println!("cargo:rustc-check-cfg=cfg(nightly)");
    // REBA: Chromium's rustc reports the nightly channel, but only allows a
    // fixed list of unstable features, which doesn't include `trusted_len`
    // that this crate would turn on.
    if false && version_meta.channel == Channel::Nightly {
        println!("cargo:rustc-cfg=nightly")
    }
}
