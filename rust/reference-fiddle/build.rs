//! Compiles tests/reference/ReferenceFiddle.c, which includes Pure Data's fiddle~.c as it is.

fn main() {
    let reference = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/reference");
    println!("cargo:rerun-if-changed={}", reference.display());
    cc::Build::new()
        .file(reference.join("ReferenceFiddle.c"))
        .include(&reference)
        // The original wasn't written for today's warnings, and isn't ours to change.
        .warnings(false)
        .compile("referencefiddle");
}
