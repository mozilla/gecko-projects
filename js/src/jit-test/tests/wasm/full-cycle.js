// |jit-test| test-also-wasm-baseline
load(libdir + "wasm.js");

wasmFullPass(`(module
    (func $test (result i32) (param i32) (param i32) (i32.add (get_local 0) (get_local 1)))
    (func $run (result i32) (call $test (i32.const 1) (i32.const ${Math.pow(2, 31) - 1})))
    (export "run" $run)
)`, -Math.pow(2, 31));
