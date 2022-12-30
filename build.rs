fn main() {
    println!("cargo:rerun-if-changed=src/lexer.h");
    cc::Build::new()
        .file("src/lexer.c")
        .define("RUST_BUILD", None)
        .compile("lexer.a");
}
