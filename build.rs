fn main() {
    println!("cargo:rerun-if-changed=src/lexer.h");
    cc::Build::new()
    .warnings(false)
    .define("RUST_BUILD", None)
    .flag_if_supported("-std=c99")
    .file("src/lexer.c")
    .compile("lexer.a");
}
