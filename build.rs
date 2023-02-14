fn main() {
    println!("cargo:rerun-if-changed=src/lexer.h");
    cc::Build::new()
    .warnings(false)
    .define("RUST_BUILD", None)
    .file("src/lexer.c")
    .compile("lexer.a");
}
