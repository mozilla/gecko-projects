/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sw=2 et tw=0 ft=c:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_Opcodes_h
#define vm_Opcodes_h

#include "mozilla/Attributes.h"

#include <stddef.h>

/*
 * [SMDOC] Bytecode Definitions
 *
 * SpiderMonkey bytecode instructions.
 *
 * To use this header, define a macro of the form:
 *
 *     #define MACRO(op, name, token, length, nuses, ndefs, format) ...
 *
 * Then `FOR_EACH_OPCODE(MACRO)` invokes `MACRO` for every opcode.
 *
 * Field        Description
 * -----        -----------
 * op           Bytecode name, which is the JSOp enumerator name
 * name         C string containing name for disassembler
 * token        Pretty-printer string, or null if ugly
 * length       Number of bytes including any immediate operands
 * nuses        Number of stack slots consumed by bytecode, -1 if variadic
 * ndefs        Number of stack slots produced by bytecode
 * format       JOF_ flags describing instruction operand layout, etc.
 *
 * For more about `format`, see the comments on the `JOF_` constants defined in
 * BytecodeUtil.h.
 *
 *
 * [SMDOC] Bytecode Invariants
 *
 * Creating scripts that do not follow the rules can lead to undefined
 * behavior. Bytecode has many consumers, not just the interpreter: JITs,
 * analyses, the debugger. That's why the rules below apply even to code that
 * can't be reached in ordinary execution (such as code after an infinite loop
 * or inside an `if (false)` block).
 *
 * The `code()` of a script must be a packed (not aligned) sequence of valid
 * instructions from start to end. Each instruction has a single byte opcode
 * followed by a number of operand bytes based on the opcode.
 *
 * ## Jump instructions
 *
 * Operands named `offset`, `forwardOffset`, or `defaultOffset` are jump
 * offsets, the distance in bytes from the start of the current instruction to
 * the start of another instruction in the same script. Operands named
 * `forwardOffset` or `defaultOffset` must be positive.
 *
 * Forward jumps must jump to a `JSOP_JUMPTARGET` instruction.  Backward jumps,
 * indicated by negative offsets, must jump to a `JSOP_LOOPHEAD` instruction.
 * Jump offsets can't be zero.
 *
 * Needless to say, scripts must not contain overlapping instruction sequences
 * (in the sense of <https://en.wikipedia.org/wiki/Overlapping_gene>).
 *
 * A script's `trynotes` and `scopeNotes` impose further constraints. Each try
 * note and each scope note marks a region of the bytecode where some invariant
 * holds, or some cleanup behavior is needed--that there's a for-in iterator in
 * a particular stack slot, for instance, which must be closed on error. All
 * paths into the span must establish that invariant. In practice, this means
 * other code never jumps into the span: the only way in is to execute the
 * bytecode instruction that sets up the invariant (in our example,
 * `JSOP_ITER`).
 *
 * If a script's `trynotes` (see "Try Notes" in JSScript.h) contain a
 * `JSTRY_CATCH` or `JSTRY_FINALLY` span, there must be a `JSOP_TRY`
 * instruction immediately before the span and a `JSOP_JUMPTARGET immediately
 * after it. Instructions must not jump to this `JSOP_JUMPTARGET`. (The VM puts
 * us there on exception.) Furthermore, the instruction sequence immediately
 * following a `JSTRY_CATCH` span must read `JUMPTARGET; EXCEPTION` or, in
 * non-function scripts, `JUMPTARGET; UNDEFINED; SETRVAL; EXCEPTION`. (These
 * instructions run with an exception pending; other instructions aren't
 * designed to handle that.)
 *
 * Unreachable instructions are allowed, but they have to follow all the rules.
 *
 * Control must not reach the end of a script. (Currently, the last instruction
 * is always JSOP_RETRVAL.)
 *
 * ## Other operands
 *
 * Operands named `nameIndex` or `atomIndex` (which appear on instructions that
 * have `JOF_ATOM` in the `format` field) must be valid indexes into
 * `script->atoms()`.
 *
 * Operands named `argc` (`JOF_ARGC`) are argument counts for call
 * instructions. `argc` must be small enough that the instruction's nuses is <=
 * the current stack depth (see "Stack depth" below).
 *
 * Operands named `argno` (`JOF_QARG`) refer to an argument of the current
 * function. `argno` must be in the range `0..script->function()->nargs()`.
 * Instructions with these operands must appear only in function scripts.
 *
 * Operands named `localno` (`JOF_LOCAL`) refer to a local variable stored in
 * the stack frame. `localno` must be in the range `0..script->nfixed()`.
 *
 * Operands named `resumeIndex` (`JOF_RESUMEINDEX`) refer to a resume point in
 * the current script. `resumeIndex` must be a valid index into
 * `script->resumeOffsets()`.
 *
 * Operands named `hops` and `slot` (`JOF_ENVCOORD`) refer a slot in an
 * `EnvironmentObject`. At run time, they must point to a fixed slot in an
 * object on the current environment chain. See `EnvironmentCoordinates`.
 *
 * Operands with the following names must be valid indexes into
 * `script->gcthings()`, and the pointer in the vector must point to the right
 * type of thing:
 *
 * -   `objectIndex` (`JOF_OBJECT`): `PlainObject*` or `ArrayObject*`
 * -   `baseobjIndex` (`JOF_OBJECT`): `PlainObject*`
 * -   `funcIndex` (`JOF_OBJECT`): `JSFunction*`
 * -   `regexpIndex` (`JOF_REGEXP`): `RegExpObject*`
 * -   `scopeIndex` (`JOF_SCOPE`): `Scope*`
 * -   `lexicalScopeIndex` (`JOF_SCOPE`): `LexicalScope*`
 * -   `withScopeIndex` (`JOF_SCOPE`): `WithScope*`
 * -   `bigIntIndex` (`JOF_BIGINT`): `BigInt*`
 *
 * Operands named `icIndex` (`JOF_ICINDEX`) must be exactly the number of
 * preceding instructions in the script that have the JOF_IC flag.
 * (Rationale: Each JOF_IC instruction has a unique entry in
 * `script->jitScript()->icEntries()`.  At run time, in the bytecode
 * interpreter, we have to find that entry. We could store the IC index as an
 * operand to each JOF_IC instruction, but it's more memory-efficient to use a
 * counter and reset the counter to `icIndex` after each jump.)
 *
 * ## Stack depth
 *
 * Each instruction has a compile-time stack depth, the number of values on the
 * interpreter stack just before executing the instruction. It isn't explicitly
 * present in the bytecode itself, but (for reachable instructions, anyway)
 * it's a function of the bytecode.
 *
 * -   The first instruction has stack depth 0.
 *
 * -   Each successor of an instruction X has a stack depth equal to
 *
 *         X's stack depth - `js::StackUses(X)` + `js::StackDefs(X)`
 *
 *     except for `JSOP_CASE` (below).
 *
 *     X's "successors" are: the next instruction in the script, if
 *     `js::FlowsIntoNext(op)` is true for X's opcode; one or more
 *     `JSOP_JUMPTARGET`s elsewhere, if X is a forward jump or
 *     `JSOP_TABLESWITCH`; and/or a `JSOP_LOOPHEAD` if it's a backward jump.
 *
 * -   `JSOP_CASE` is a special case because its stack behavior is eccentric.
 *     The formula above is correct for the next instruction. The jump target
 *     has a stack depth that is 1 less.
 *
 * -   See `JSOP_GOSUB` for another special case.
 *
 * -   The `JSOP_JUMPTARGET` instruction immediately following a `JSTRY_CATCH`
 *     or `JSTRY_FINALLY` span has the same stack depth as the `JSOP_TRY`
 *     instruction that precedes the span.
 *
 *     Every instruction covered by the `JSTRY_CATCH` or `JSTRY_FINALLY` span
 *     must have a stack depth >= that value, so that error recovery is
 *     guaranteed to find enough values on the stack to resume there.
 *
 * -   `script->nslots() - script->nfixed()` must be >= the maximum stack
 *     depth of any instruction in `script`.  (The stack frame must be big
 *     enough to run the code.)
 *
 * `BytecodeParser::parse()` computes stack depths for every reachable
 * instruction in a script.
 *
 * ## Scopes and environments
 *
 * As with stack depth, each instruction has a static scope, which is a
 * compile-time characterization of the eventual run-time environment chain
 * when that instruction executes. Just as every instruction has a stack budget
 * (nuses/ndefs), every instruction either pushes a scope, pops a scope, or
 * neither. The same successor relation applies as above.
 *
 * Every scope used in a script is stored in the `JSScript::gcthings()` vector.
 * They can be accessed using `getScope(index)` if you know what `index` to
 * pass.
 *
 * The scope of every instruction (that's reachable via the successor relation)
 * is given in two independent ways: by the bytecode itself and by the scope
 * notes. The two sources must agree.
 *
 * ## Further rules
 *
 * All reachable instructions must be reachable without taking any backward
 * edges.
 *
 * Instructions with the `JOF_CHECKSLOPPY` flag must not be used in strict mode
 * code. `JOF_CHECKSTRICT` instructions must not be used in nonstrict code.
 *
 * Many instructions have their own additional rules. These are documented on
 * the various opcodes below (look for the word "must").
 */

// clang-format off
/*
 * SpiderMonkey bytecode categorization (as used in generated documentation):
 *
 * [Index]
 *   [Constants]
 *   [Expressions]
 *     Unary operators
 *     Binary operators
 *     Conversions
 *     Other expressions
 *   [Objects]
 *     Creating objects
 *     Defining properties
 *     Accessing properties
 *     Super
 *     Enumeration
 *     Iteration
 *     SetPrototype
 *     Array literals
 *     RegExp literals
 *   [Functions]
 *     Creating functions
 *     Creating constructors
 *     Calls
 *     Generators and async functions
 *   [Control flow]
 *     Jump targets
 *     Jumps
 *     Return
 *     Exceptions
 *   [Variables and scopes]
 *     Initialization
 *     Looking up bindings
 *     Getting binding values
 *     Setting binding values
 *     Entering and leaving environments
 *     Creating and deleting bindings
 *     Function environment setup
 *   [Stack operations]
 *   [Other]
 */
// clang-format on

// clang-format off
#define FOR_EACH_OPCODE(MACRO) \
    /*
     * Push `undefined`.
     *
     *   Category: Constants
     *   Operands:
     *   Stack: => undefined
     */ \
    MACRO(JSOP_UNDEFINED, js_undefined_str, "", 1, 0, 1, JOF_BYTE) \
    /*
     * Push `null`.
     *
     *   Category: Constants
     *   Operands:
     *   Stack: => null
     */ \
    MACRO(JSOP_NULL, js_null_str, js_null_str, 1, 0, 1, JOF_BYTE) \
    /*
     * Push a boolean constant.
     *
     *   Category: Constants
     *   Operands:
     *   Stack: => true/false
     */ \
    MACRO(JSOP_FALSE, js_false_str, js_false_str, 1, 0, 1, JOF_BYTE) \
    MACRO(JSOP_TRUE, js_true_str, js_true_str, 1, 0, 1, JOF_BYTE) \
    /*
     * Push the `int32_t` immediate operand as an `Int32Value`.
     *
     * `JSOP_ZERO`, `JSOP_ONE`, `JSOP_INT8`, `JSOP_UINT16`, and `JSOP_UINT24`
     * are all compact encodings for `JSOP_INT32`.
     *
     *   Category: Constants
     *   Operands: int32_t val
     *   Stack: => val
     */ \
    MACRO(JSOP_INT32, "int32", NULL, 5, 0, 1, JOF_INT32) \
    /*
     * Push the number `0`.
     *
     *   Category: Constants
     *   Operands:
     *   Stack: => 0
     */ \
    MACRO(JSOP_ZERO, "zero", "0", 1, 0, 1, JOF_BYTE) \
    /*
     * Push the number `1`.
     *
     *   Category: Constants
     *   Operands:
     *   Stack: => 1
     */ \
    MACRO(JSOP_ONE, "one", "1", 1, 0, 1, JOF_BYTE) \
    /*
     * Push the `int8_t` immediate operand as an `Int32Value`.
     *
     *   Category: Constants
     *   Operands: int8_t val
     *   Stack: => val
     */ \
    MACRO(JSOP_INT8, "int8", NULL, 2, 0, 1, JOF_INT8) \
    /*
     * Push the `uint16_t` immediate operand as an `Int32Value`.
     *
     *   Category: Constants
     *   Operands: uint16_t val
     *   Stack: => val
     */ \
    MACRO(JSOP_UINT16, "uint16", NULL, 3, 0, 1, JOF_UINT16) \
    /*
     * Push the `uint24_t` immediate operand as an `Int32Value`.
     *
     *   Category: Constants
     *   Operands: uint24_t val
     *   Stack: => val
     */ \
    MACRO(JSOP_UINT24, "uint24", NULL, 4, 0, 1, JOF_UINT24) \
    /*
     * Push the 64-bit floating-point immediate operand as a `DoubleValue`.
     *
     * If the operand is a NaN, it must be the canonical NaN (see
     * `JS::detail::CanonicalizeNaN`).
     *
     *   Category: Constants
     *   Operands: double val
     *   Stack: => val
     */ \
    MACRO(JSOP_DOUBLE, "double", NULL, 9, 0, 1, JOF_DOUBLE) \
    /*
     * Push the BigInt constant `script->getBigInt(bigIntIndex)`.
     *
     *   Category: Constants
     *   Operands: uint32_t bigIntIndex
     *   Stack: => bigint
     */ \
    MACRO(JSOP_BIGINT, "bigint", NULL, 5, 0, 1, JOF_BIGINT) \
    /*
     * Push the string constant `script->getAtom(atomIndex)`.
     *
     *   Category: Constants
     *   Operands: uint32_t atomIndex
     *   Stack: => string
     */ \
    MACRO(JSOP_STRING, "string", NULL, 5, 0, 1, JOF_ATOM) \
    /*
     * Push a well-known symbol.
     *
     * `symbol` must be in range for `JS::SymbolCode`.
     *
     *   Category: Constants
     *   Operands: uint8_t symbol (the JS::SymbolCode of the symbol to use)
     *   Stack: => symbol
     */ \
    MACRO(JSOP_SYMBOL, "symbol", NULL, 2, 0, 1, JOF_UINT8) \
    /*
     * Pop the top value on the stack, discard it, and push `undefined`.
     *
     * Implements: [The `void` operator][1], step 3.
     *
     * [1]: https://tc39.es/ecma262/#sec-void-operator
     *
     *   Category: Expressions
     *   Type: Unary operators
     *   Operands:
     *   Stack: val => undefined
     */ \
    MACRO(JSOP_VOID, js_void_str, NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * [The `typeof` operator][1].
     *
     * Infallible. The result is always a string that depends on the [type][2]
     * of `val`.
     *
     * `JSOP_TYPEOF` and `JSOP_TYPEOFEXPR` are the same except
     * that--amazingly--`JSOP_TYPEOF` affects the behavior of an immediately
     * *preceding* `JSOP_GETNAME` or `JSOP_GETGNAME` instruction! This is how
     * we implement [`typeof`][1] step 2, making `typeof nonExistingVariable`
     * return `"undefined"` instead of throwing a ReferenceError.
     *
     * In a global scope:
     *
     * -   `typeof x` compiles to `GETGNAME "x"; TYPEOF`.
     * -   `typeof (0, x)` compiles to `GETGNAME "x"; TYPEOFEXPR`.
     *
     * Emitting the same bytecode for these two expressions would be a bug.
     * Per spec, the latter throws a ReferenceError if `x` doesn't exist.
     *
     * [1]: https://tc39.es/ecma262/#sec-typeof-operator
     * [2]: https://tc39.es/ecma262/#sec-ecmascript-language-types
     *
     *   Category: Expressions
     *   Type: Unary operators
     *   Operands:
     *   Stack: val => (typeof val)
     */ \
    MACRO(JSOP_TYPEOF, js_typeof_str, NULL, 1, 1, 1, JOF_BYTE|JOF_DETECTING|JOF_IC) \
    MACRO(JSOP_TYPEOFEXPR, "typeofexpr", NULL, 1, 1, 1, JOF_BYTE|JOF_DETECTING|JOF_IC) \
    /*
     * [The unary `+` operator][1].
     *
     * `+val` doesn't do any actual math. It just calls [ToNumber][2](val).
     *
     * The conversion can call `.toString()`/`.valueOf()` methods and can
     * throw. The result on success is always a Number. (Per spec, unary `-`
     * supports BigInts, but unary `+` does not.)
     *
     * [1]: https://tc39.es/ecma262/#sec-unary-plus-operator
     * [2]: https://tc39.es/ecma262/#sec-tonumber
     *
     *   Category: Expressions
     *   Type: Unary operators
     *   Operands:
     *   Stack: val => (+val)
     */ \
    MACRO(JSOP_POS, "pos", "+ ", 1, 1, 1, JOF_BYTE) \
    /*
     * [The unary `-` operator][1].
     *
     * Convert `val` to a numeric value, then push `-val`. The conversion can
     * call `.toString()`/`.valueOf()` methods and can throw. The result on
     * success is always numeric.
     *
     * [1]: https://tc39.es/ecma262/#sec-unary-minus-operator
     *
     *   Category: Expressions
     *   Type: Unary operators
     *   Operands:
     *   Stack: val => (-val)
     */ \
    MACRO(JSOP_NEG, "neg", "- ", 1, 1, 1, JOF_BYTE|JOF_IC) \
    /*
     * [The bitwise NOT operator][1] (`~`).
     *
     * `val` is converted to an integer, then bitwise negated. The conversion
     * can call `.toString()`/`.valueOf()` methods and can throw. The result on
     * success is always an Int32 or BigInt value.
     *
     * [1]: https://tc39.es/ecma262/#sec-bitwise-not-operator
     *
     *   Category: Expressions
     *   Type: Unary operators
     *   Operands:
     *   Stack: val => (~val)
     */ \
    MACRO(JSOP_BITNOT, "bitnot", "~", 1, 1, 1, JOF_BYTE|JOF_IC) \
    /*
     * [The logical NOT operator][1] (`!`).
     *
     * `val` is first converted with [ToBoolean][2], then logically
     * negated. The result is always a boolean value. This does not call
     * user-defined methods and can't throw.
     *
     * [1]: https://tc39.es/ecma262/#sec-logical-not-operator
     * [2]: https://tc39.es/ecma262/#sec-toboolean
     *
     *   Category: Expressions
     *   Type: Unary operators
     *   Operands:
     *   Stack: val => (!val)
     */ \
    MACRO(JSOP_NOT, "not", "!", 1, 1, 1, JOF_BYTE|JOF_DETECTING|JOF_IC) \
    /*
     * [Binary bitwise operations][1] (`|`, `^`, `&`).
     *
     * The arguments are converted to integers first. The conversion can call
     * `.toString()`/`.valueOf()` methods and can throw. The result on success
     * is always an Int32 or BigInt Value.
     *
     * [1]: https://tc39.es/ecma262/#sec-binary-bitwise-operators
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: lval, rval => (lval OP rval)
     */ \
    MACRO(JSOP_BITOR, "bitor", "|",  1, 2, 1, JOF_BYTE|JOF_IC) \
    MACRO(JSOP_BITXOR, "bitxor", "^", 1, 2, 1, JOF_BYTE|JOF_IC) \
    MACRO(JSOP_BITAND, "bitand", "&", 1, 2, 1, JOF_BYTE|JOF_IC) \
    /*
     * Loose equality operators (`==` and `!=`).
     *
     * Pop two values, compare them, and push the boolean result. The
     * comparison may perform conversions that call `.toString()`/`.valueOf()`
     * methods and can throw.
     *
     * Implements: [Abstract Equality Comparison][1].
     *
     * [1]: https://tc39.es/ecma262/#sec-abstract-equality-comparison
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: lval, rval => (lval OP rval)
     */ \
    MACRO(JSOP_EQ, "eq", "==", 1, 2, 1, JOF_BYTE|JOF_DETECTING|JOF_IC) \
    MACRO(JSOP_NE, "ne", "!=", 1, 2, 1, JOF_BYTE|JOF_DETECTING|JOF_IC) \
    /*
     * Strict equality operators (`===` and `!==`).
     *
     * Pop two values, check whether they're equal, and push the boolean
     * result. This does not call user-defined methods and can't throw
     * (except possibly due to OOM while flattening a string).
     *
     * Implements: [Strict Equality Comparison][1].
     *
     * [1]: https://tc39.es/ecma262/#sec-strict-equality-comparison
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: lval, rval => (lval OP rval)
     */ \
    MACRO(JSOP_STRICTEQ, "stricteq", "===", 1, 2, 1, JOF_BYTE|JOF_DETECTING|JOF_IC) \
    MACRO(JSOP_STRICTNE, "strictne", "!==", 1, 2, 1, JOF_BYTE|JOF_DETECTING|JOF_IC) \
    /*
     * Relative operators (`<`, `>`, `<=`, `>=`).
     *
     * Pop two values, compare them, and push the boolean result. The
     * comparison may perform conversions that call `.toString()`/`.valueOf()`
     * methods and can throw.
     *
     * Implements: [Relational Operators: Evaluation][1].
     *
     * [1]: https://tc39.es/ecma262/#sec-relational-operators-runtime-semantics-evaluation
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: lval, rval => (lval OP rval)
     */ \
    MACRO(JSOP_LT, "lt", "<",  1, 2, 1, JOF_BYTE|JOF_IC) \
    MACRO(JSOP_GT, "gt", ">",  1, 2, 1, JOF_BYTE|JOF_IC) \
    MACRO(JSOP_LE, "le", "<=", 1, 2, 1, JOF_BYTE|JOF_IC) \
    MACRO(JSOP_GE, "ge", ">=", 1, 2, 1, JOF_BYTE|JOF_IC) \
    /*
     * [The `instanceof` operator][1].
     *
     * This throws a `TypeError` if `target` is not an object. It calls
     * `target[Symbol.hasInstance](value)` if the method exists. On success,
     * the result is always a boolean value.
     *
     * [1]: https://tc39.es/ecma262/#sec-instanceofoperator
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: value, target => (value instanceof target)
     */ \
    MACRO(JSOP_INSTANCEOF, js_instanceof_str, js_instanceof_str, 1, 2, 1, JOF_BYTE|JOF_IC) \
    /*
     * [The `in` operator][1].
     *
     * Push `true` if `obj` has a property with the key `id`. Otherwise push `false`.
     *
     * This throws a `TypeError` if `obj` is not an object. This can fire
     * proxy hooks and can throw. On success, the result is always a boolean
     * value.
     *
     * [1]: https://tc39.es/ecma262/#sec-relational-operators-runtime-semantics-evaluation
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: id, obj => (id in obj)
     */ \
    MACRO(JSOP_IN, js_in_str, js_in_str, 1, 2, 1, JOF_BYTE|JOF_IC) \
    /*
     * [Bitwise shift operators][1] (`<<`, `>>`, `>>>`).
     *
     * Pop two values, convert them to integers, perform a bitwise shift, and
     * push the result.
     *
     * Conversion can call `.toString()`/`.valueOf()` methods and can throw.
     * The result on success is always an Int32 or BigInt Value.
     *
     * [1]: https://tc39.es/ecma262/#sec-bitwise-shift-operators
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: lval, rval => (lval OP rval)
     */ \
    MACRO(JSOP_LSH, "lsh", "<<", 1, 2, 1, JOF_BYTE|JOF_IC) \
    MACRO(JSOP_RSH, "rsh", ">>", 1, 2, 1, JOF_BYTE|JOF_IC) \
    MACRO(JSOP_URSH, "ursh", ">>>", 1, 2, 1, JOF_BYTE|JOF_IC) \
    /*
     * [The binary `+` operator][1].
     *
     * Pop two values, convert them to primitive values, add them, and push the
     * result. If both values are numeric, add them; if either is a
     * string, do string concatenation instead.
     *
     * The conversion can call `.toString()`/`.valueOf()` methods and can throw.
     *
     * [1]: https://tc39.es/ecma262/#sec-addition-operator-plus-runtime-semantics-evaluation
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: lval, rval => (lval + rval)
     */ \
    MACRO(JSOP_ADD, "add", "+", 1, 2, 1, JOF_BYTE|JOF_IC) \
    /*
     * [The binary `-` operator][1].
     *
     * Pop two values, convert them to numeric values, subtract the top value
     * from the other one, and push the result.
     *
     * The conversion can call `.toString()`/`.valueOf()` methods and can
     * throw. On success, the result is always numeric.
     *
     * [1]: https://tc39.es/ecma262/#sec-subtraction-operator-minus-runtime-semantics-evaluation
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: lval, rval => (lval - rval)
     */ \
    MACRO(JSOP_SUB, "sub", "-", 1, 2, 1, JOF_BYTE|JOF_IC) \
    /*
     * Add or subtract 1.
     *
     * `val` must already be a numeric value, such as the result of
     * `JSOP_TONUMERIC`.
     *
     * Implements: [The `++` and `--` operators][1], step 3 of each algorithm.
     *
     * [1]: https://tc39.es/ecma262/#sec-postfix-increment-operator
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: val => (val +/- 1)
     */ \
    MACRO(JSOP_INC, "inc", NULL, 1, 1, 1, JOF_BYTE|JOF_IC) \
    MACRO(JSOP_DEC, "dec", NULL, 1, 1, 1, JOF_BYTE|JOF_IC) \
    /*
     * [The multiplicative operators][1] (`*`, `/`, `%`).
     *
     * Pop two values, convert them to numeric values, do math, and push the
     * result.
     *
     * The conversion can call `.toString()`/`.valueOf()` methods and can
     * throw. On success, the result is always numeric.
     *
     * [1]: https://tc39.es/ecma262/#sec-multiplicative-operators-runtime-semantics-evaluation
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: lval, rval => (lval OP rval)
     */ \
    MACRO(JSOP_MUL, "mul", "*", 1, 2, 1, JOF_BYTE|JOF_IC) \
    MACRO(JSOP_DIV, "div", "/", 1, 2, 1, JOF_BYTE|JOF_IC) \
    MACRO(JSOP_MOD, "mod", "%", 1, 2, 1, JOF_BYTE|JOF_IC) \
    /*
     * [The exponentiation operator][1] (`**`).
     *
     * Pop two values, convert them to numeric values, do exponentiation, and
     * push the result. The top value is the exponent.
     *
     * The conversion can call `.toString()`/`.valueOf()` methods and can
     * throw. This throws a RangeError if both values are BigInts and the
     * exponent is negative.
     *
     * [1]: https://tc39.es/ecma262/#sec-exp-operator
     *
     *   Category: Expressions
     *   Type: Binary operators
     *   Operands:
     *   Stack: lval, rval => (lval ** rval)
     */ \
    MACRO(JSOP_POW, "pow", "**", 1, 2, 1, JOF_BYTE|JOF_IC) \
    /*
     * Convert a value to a property key.
     *
     * Implements: [ToPropertyKey][1], except that if the result would be the
     * string representation of some integer in the range 0..2^31, we push the
     * corresponding Int32 value instead. This is because the spec insists that
     * array indices are strings, whereas for us they are integers.
     *
     * This is used for code like `++obj[index]`, which must do both a
     * `JSOP_GETELEM` and a `JSOP_SETELEM` with the same property key. Both
     * instructions would convert `index` to a property key for us, but the
     * spec says to convert it only once.
     *
     * The conversion can call `.toString()`/`.valueOf()` methods and can
     * throw.
     *
     * [1]: https://tc39.es/ecma262/#sec-topropertykey
     *
     *   Category: Expressions
     *   Type: Conversions
     *   Operands:
     *   Stack: propertyNameValue => propertyKey
     */ \
    MACRO(JSOP_TOID, "toid", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Convert a value to a numeric value (a Number or BigInt).
     *
     * Implements: [ToNumeric][1](val).
     *
     * Note: This is used to implement [`++` and `--`][2]. Surprisingly, it's
     * not possible to get the right behavior using `JSOP_ADD` and `JSOP_SUB`
     * alone. For one thing, `JSOP_ADD` sometimes does string concatenation,
     * while `++` always does numeric addition. More fundamentally, the result
     * of evaluating `--x` is ToNumeric(old value of `x`), a value that the
     * sequence `GETLOCAL "x"; ONE; SUB; SETLOCAL "x"` does not give us.
     *
     * [1]: https://tc39.es/ecma262/#sec-tonumeric
     * [2]: https://tc39.es/ecma262/#sec-postfix-increment-operator
     *
     *   Category: Expressions
     *   Type: Conversions
     *   Operands:
     *   Stack: val => ToNumeric(val)
     */ \
    MACRO(JSOP_TONUMERIC, "tonumeric", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Convert a value to a string.
     *
     * Implements: [ToString][1](val).
     *
     * Note: This is used in code for template literals, like `${x}${y}`. Each
     * substituted value must be converted using ToString. `JSOP_ADD` by itself
     * would do a slightly wrong kind of conversion (hint="number" rather than
     * hint="string").
     *
     * [1]: https://tc39.es/ecma262/#sec-tostring
     *
     *   Category: Expressions
     *   Operands: Conversions
     *   Stack: val => ToString(val)
     */ \
    MACRO(JSOP_TOSTRING, "tostring", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Push the global `this` value. Not to be confused with the `globalThis`
     * property on the global.
     *
     * This must be used only in scopes where `this` refers to the global
     * `this`.
     *
     *   Category: Expressions
     *   Type: Other expressions
     *   Operands:
     *   Stack: => this
     */ \
    MACRO(JSOP_GLOBALTHIS, "globalthis", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Push the value of `new.target`.
     *
     * The result is a constructor or `undefined`.
     *
     * This must be used only in scripts where `new.target` is allowed:
     * non-arrow function scripts and other scripts that have a non-arrow
     * function script on the scope chain.
     *
     * Implements: [GetNewTarget][1].
     *
     * [1]: https://tc39.es/ecma262/#sec-getnewtarget
     *
     *   Category: Expressions
     *   Type: Other expressions
     *   Operands:
     *   Stack: => new.target
     */ \
    MACRO(JSOP_NEWTARGET, "newtarget", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Dynamic import of the module specified by the string value on the top of
     * the stack.
     *
     * Implements: [Import Calls][1].
     *
     * [1]: https://tc39.es/ecma262/#sec-import-calls
     *
     *   Category: Expressions
     *   Type: Other expressions
     *   Operands:
     *   Stack: moduleId => promise
     */ \
    MACRO(JSOP_DYNAMIC_IMPORT, "dynamic-import", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Push the `import.meta` object.
     *
     * This must be used only in module code.
     *
     *   Category: Expressions
     *   Type: Other expressions
     *   Operands:
     *   Stack: => import.meta
     */ \
    MACRO(JSOP_IMPORTMETA, "importmeta", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Create and push a new object with no properties.
     *
     * (This opcode has 4 unused bytes so it can be easily turned into
     * `JSOP_NEWOBJECT` during bytecode generation.)
     *
     *   Category: Objects
     *   Type: Creating objects
     *   Operands: uint32_t _unused
     *   Stack: => obj
     */ \
    MACRO(JSOP_NEWINIT, "newinit", NULL, 5, 0, 1, JOF_UINT32|JOF_IC) \
    /*
     * Create and push a new object of a predetermined shape.
     *
     * The new object has the shape of the template object
     * `script->getObject(baseobjIndex)`. Subsequent `INITPROP` instructions
     * must fill in all slots of the new object before it is used in any other
     * way.
     *
     * For `JSOP_NEWOBJECT`, the new object has a group based on the allocation
     * site (or a new group if the template's group is a singleton). For
     * `JSOP_NEWOBJECT_WITHGROUP`, the new object has the same group as the
     * template object.
     *
     *   Category: Objects
     *   Type: Creating objects
     *   Operands: uint32_t baseobjIndex
     *   Stack: => obj
     */ \
    MACRO(JSOP_NEWOBJECT, "newobject", NULL, 5, 0, 1, JOF_OBJECT|JOF_IC) \
    MACRO(JSOP_NEWOBJECT_WITHGROUP, "newobjectwithgroup", NULL, 5, 0, 1, JOF_OBJECT|JOF_IC) \
    /*
     * Push a preconstructed object.
     *
     * Going one step further than `JSOP_NEWOBJECT`, this instruction doesn't
     * just reuse the shape--it actually pushes the preconstructed object
     * `script->getObject(objectIndex)` right onto the stack. The object must
     * be a singleton `PlainObject` or `ArrayObject`.
     *
     * The spec requires that an *ObjectLiteral* or *ArrayLiteral* creates a
     * new object every time it's evaluated, so this instruction must not be
     * used anywhere it might be executed more than once.
     *
     * There's a shell-only option, `newGlobal({cloneSingletons: true})`, that
     * makes this instruction do a deep copy of the object. A few tests use it.
     *
     *   Category: Objects
     *   Type: Creating objects
     *   Operands: uint32_t objectIndex
     *   Stack: => obj
     */ \
    MACRO(JSOP_OBJECT, "object", NULL, 5, 0, 1, JOF_OBJECT) \
    /*
     * Create and push a new ordinary object with the provided [[Prototype]].
     *
     * This is used to create the `.prototype` object for derived classes.
     *
     *   Category: Objects
     *   Type: Creating objects
     *   Operands:
     *   Stack: proto => obj
     */ \
    MACRO(JSOP_OBJWITHPROTO, "objwithproto", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Define a data property on an object.
     *
     * `obj` must be an object.
     *
     * Implements: [CreateDataPropertyOrThrow][1] as used in
     * [PropertyDefinitionEvaluation][2] of regular and shorthand
     * *PropertyDefinition*s.
     *
     *    [1]: https://tc39.es/ecma262/#sec-createdatapropertyorthrow
     *    [2]: https://tc39.es/ecma262/#sec-object-initializer-runtime-semantics-propertydefinitionevaluation
     *
     *   Category: Objects
     *   Type: Defining properties
     *   Operands: uint32_t nameIndex
     *   Stack: obj, val => obj
     */ \
    MACRO(JSOP_INITPROP, "initprop", NULL, 5, 2, 1, JOF_ATOM|JOF_PROP|JOF_PROPINIT|JOF_DETECTING|JOF_IC) \
    /*
     * Like `JSOP_INITPROP`, but define a non-enumerable property.
     *
     * This is used to define class methods.
     *
     * Implements: [PropertyDefinitionEvaluation][1] for methods, steps 3 and
     * 4, when *enumerable* is false.
     *
     *    [1]: https://tc39.es/ecma262/#sec-method-definitions-runtime-semantics-propertydefinitionevaluation
     *
     *   Category: Objects
     *   Type: Defining properties
     *   Operands: uint32_t nameIndex
     *   Stack: obj, val => obj
     */ \
    MACRO(JSOP_INITHIDDENPROP, "inithiddenprop", NULL, 5, 2, 1, JOF_ATOM|JOF_PROP|JOF_PROPINIT|JOF_DETECTING|JOF_IC) \
    /*
     * Like `JSOP_INITPROP`, but define a non-enumerable, non-writable,
     * non-configurable property.
     *
     * This is used to define the `.prototype` property on classes.
     *
     * Implements: [MakeConstructor][1], step 8, when *writablePrototype* is
     * false.
     *
     *    [1]: https://tc39.es/ecma262/#sec-makeconstructor
     *
     *   Category: Objects
     *   Type: Defining properties
     *   Operands: uint32_t nameIndex
     *   Stack: obj, val => obj
     */ \
    MACRO(JSOP_INITLOCKEDPROP, "initlockedprop", NULL, 5, 2, 1, JOF_ATOM|JOF_PROP|JOF_PROPINIT|JOF_DETECTING|JOF_IC) \
    /*
     * Define a data property on `obj` with property key `id` and value `val`.
     *
     * Implements: [CreateDataPropertyOrThrow][1]. This instruction is used for
     * object literals like `{0: val}` and `{[id]: val}`, and methods like
     * `*[Symbol.iterator]() {}`.
     *
     * `JSOP_INITHIDDENELEM` is the same but defines a non-enumerable property,
     * for class methods.
     *
     *    [1]: https://tc39.es/ecma262/#sec-createdatapropertyorthrow
     *
     *   Category: Objects
     *   Type: Defining properties
     *   Operands:
     *   Stack: obj, id, val => obj
     */ \
    MACRO(JSOP_INITELEM, "initelem", NULL, 1, 3, 1, JOF_BYTE|JOF_ELEM|JOF_PROPINIT|JOF_DETECTING|JOF_IC) \
    MACRO(JSOP_INITHIDDENELEM, "inithiddenelem", NULL, 1, 3, 1, JOF_BYTE|JOF_ELEM|JOF_PROPINIT|JOF_DETECTING|JOF_IC) \
    /*
     * Define an accessor property on `obj` with the given `getter`.
     * `nameIndex` gives the property name.
     *
     * `JSOP_INITHIDDENPROP_GETTER` is the same but defines a non-enumerable
     * property, for getters in classes.
     *
     *   Category: Objects
     *   Type: Defining properties
     *   Operands: uint32_t nameIndex
     *   Stack: obj, getter => obj
     */ \
    MACRO(JSOP_INITPROP_GETTER, "initprop_getter", NULL, 5, 2, 1, JOF_ATOM|JOF_PROP|JOF_PROPINIT|JOF_DETECTING) \
    MACRO(JSOP_INITHIDDENPROP_GETTER, "inithiddenprop_getter", NULL, 5, 2, 1, JOF_ATOM|JOF_PROP|JOF_PROPINIT|JOF_DETECTING) \
    /*
     * Define an accessor property on `obj` with property key `id` and the given `getter`.
     *
     * This is used to implement getters like `get [id]() {}` or `get 0() {}`.
     *
     * `JSOP_INITHIDDENELEM_GETTER` is the same but defines a non-enumerable
     * property, for getters in classes.
     *
     *   Category: Objects
     *   Type: Defining properties
     *   Operands:
     *   Stack: obj, id, getter => obj
     */ \
    MACRO(JSOP_INITELEM_GETTER, "initelem_getter", NULL, 1, 3, 1, JOF_BYTE|JOF_ELEM|JOF_PROPINIT|JOF_DETECTING) \
    MACRO(JSOP_INITHIDDENELEM_GETTER, "inithiddenelem_getter", NULL, 1, 3, 1, JOF_BYTE|JOF_ELEM|JOF_PROPINIT|JOF_DETECTING) \
    /*
     * Define an accessor property on `obj` with the given `setter`.
     *
     * This is used to implement ordinary setters like `set foo(v) {}`.
     *
     * `JSOP_INITHIDDENPROP_SETTER` is the same but defines a non-enumerable
     * property, for setters in classes.
     *
     *   Category: Objects
     *   Type: Defining properties
     *   Operands: uint32_t nameIndex
     *   Stack: obj, setter => obj
     */ \
    MACRO(JSOP_INITPROP_SETTER, "initprop_setter", NULL, 5, 2, 1, JOF_ATOM|JOF_PROP|JOF_PROPINIT|JOF_DETECTING) \
    MACRO(JSOP_INITHIDDENPROP_SETTER, "inithiddenprop_setter", NULL, 5, 2, 1, JOF_ATOM|JOF_PROP|JOF_PROPINIT|JOF_DETECTING) \
    /*
     * Define an accesssor property on `obj` with property key `id` and the
     * given `setter`.
     *
     * This is used to implement setters with computed property keys or numeric
     * keys.
     *
     * `JSOP_INITHIDDENELEM_SETTER` is the same but defines a non-enumerable
     * property, for setters in classes.
     *
     *   Category: Objects
     *   Type: Defining properties
     *   Operands:
     *   Stack: obj, id, setter => obj
     */ \
    MACRO(JSOP_INITELEM_SETTER, "initelem_setter", NULL, 1, 3, 1, JOF_BYTE|JOF_ELEM|JOF_PROPINIT|JOF_DETECTING) \
    MACRO(JSOP_INITHIDDENELEM_SETTER, "inithiddenelem_setter", NULL, 1, 3, 1, JOF_BYTE|JOF_ELEM|JOF_PROPINIT|JOF_DETECTING) \
    /*
     * Get the value of the property `obj.name`. This can call getters and
     * proxy traps.
     *
     * `JSOP_CALLPROP` is exactly like `JSOP_GETPROP` but hints to the VM that we're
     * getting a method in order to call it.
     *
     * Implements: [GetV][1], [GetValue][2] step 5.
     *
     * [1]: https://tc39.es/ecma262/#sec-getv
     * [2]: https://tc39.es/ecma262/#sec-getvalue
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands: uint32_t nameIndex
     *   Stack: obj => obj[name]
     */ \
    MACRO(JSOP_GETPROP, "getprop", NULL, 5, 1, 1, JOF_ATOM|JOF_PROP|JOF_TYPESET|JOF_IC) \
    MACRO(JSOP_CALLPROP, "callprop", NULL, 5, 1, 1, JOF_ATOM|JOF_PROP|JOF_TYPESET|JOF_IC) \
    /*
     * Get the value of the property `obj[key]`.
     *
     * `JSOP_CALLELEM` is exactly like `JSOP_GETELEM` but hints to the VM that
     * we're getting a method in order to call it.
     *
     * Implements: [GetV][1], [GetValue][2] step 5.
     *
     * [1]: https://tc39.es/ecma262/#sec-getv
     * [2]: https://tc39.es/ecma262/#sec-getvalue
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands:
     *   Stack: obj, key => obj[key]
     */ \
    MACRO(JSOP_GETELEM, "getelem", NULL, 1, 2, 1, JOF_BYTE|JOF_ELEM|JOF_TYPESET|JOF_IC) \
    MACRO(JSOP_CALLELEM, "callelem", NULL, 1, 2, 1, JOF_BYTE|JOF_ELEM|JOF_TYPESET|JOF_IC) \
    /*
     * Push the value of `obj.length`.
     *
     * `nameIndex` must be the index of the atom `"length"`. This then behaves
     * exactly like `JSOP_GETPROP`.
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands: uint32_t nameIndex
     *   Stack: obj => obj.length
     */ \
    MACRO(JSOP_LENGTH, "length", NULL, 5, 1, 1, JOF_ATOM|JOF_PROP|JOF_TYPESET|JOF_IC) \
    /*
     * Non-strict assignment to a property, `obj.name = val`.
     *
     * This throws a TypeError if `obj` is null or undefined. If it's a
     * primitive value, the property is set on ToObject(`obj`), typically with
     * no effect.
     *
     * Implements: [PutValue][1] step 6 for non-strict code.
     *
     * [1]: https://tc39.es/ecma262/#sec-putvalue
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands: uint32_t nameIndex
     *   Stack: obj, val => val
     */ \
    MACRO(JSOP_SETPROP, "setprop", NULL, 5, 2, 1, JOF_ATOM|JOF_PROP|JOF_PROPSET|JOF_DETECTING|JOF_CHECKSLOPPY|JOF_IC) \
    /*
     * Like `JSOP_SETPROP`, but for strict mode code. Throw a TypeError if
     * `obj[key]` exists but is non-writable, if it's an accessor property with
     * no setter, or if `obj` is a primitive value.
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands: uint32_t nameIndex
     *   Stack: obj, val => val
     */ \
    MACRO(JSOP_STRICTSETPROP, "strict-setprop", NULL, 5, 2, 1, JOF_ATOM|JOF_PROP|JOF_PROPSET|JOF_DETECTING|JOF_CHECKSTRICT|JOF_IC) \
    /*
     * Non-strict assignment to a property, `obj[key] = val`.
     *
     * Implements: [PutValue][1] step 6 for non-strict code.
     *
     * [1]: https://tc39.es/ecma262/#sec-putvalue
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands:
     *   Stack: obj, key, val => val
     */ \
    MACRO(JSOP_SETELEM, "setelem", NULL, 1, 3, 1, JOF_BYTE|JOF_ELEM|JOF_PROPSET|JOF_DETECTING|JOF_CHECKSLOPPY|JOF_IC) \
    /*
     * Like `JSOP_SETELEM`, but for strict mode code. Throw a TypeError if
     * `obj[key]` exists but is non-writable, if it's an accessor property with
     * no setter, or if `obj` is a primitive value.
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands:
     *   Stack: obj, key, val => val
     */ \
    MACRO(JSOP_STRICTSETELEM, "strict-setelem", NULL, 1, 3, 1, JOF_BYTE|JOF_ELEM|JOF_PROPSET|JOF_DETECTING|JOF_CHECKSTRICT|JOF_IC) \
    /*
     * Delete a property from `obj`. Push true on success, false if the
     * property existed but could not be deleted. This implements `delete
     * obj.name` in non-strict code.
     *
     * Throws if `obj` is null or undefined. Can call proxy traps.
     *
     * Implements: [`delete obj.propname`][1] step 5 in non-strict code.
     *
     * [1]: https://tc39.es/ecma262/#sec-delete-operator-runtime-semantics-evaluation
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands: uint32_t nameIndex
     *   Stack: obj => succeeded
     */ \
    MACRO(JSOP_DELPROP, "delprop", NULL, 5, 1, 1, JOF_ATOM|JOF_PROP|JOF_CHECKSLOPPY) \
    /*
     * Like `JSOP_DELPROP`, but for strict mode code. Push `true` on success,
     * else throw a TypeError.
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands: uint32_t nameIndex
     *   Stack: obj => succeeded
     */ \
    MACRO(JSOP_STRICTDELPROP, "strict-delprop", NULL, 5, 1, 1, JOF_ATOM|JOF_PROP|JOF_CHECKSTRICT) \
    /*
     * Delete the property `obj[key]` and push `true` on success, `false`
     * if the property existed but could not be deleted.
     *
     * This throws if `obj` is null or undefined. Can call proxy traps.
     *
     * Implements: [`delete obj[key]`][1] step 5 in non-strict code.
     *
     * [1]: https://tc39.es/ecma262/#sec-delete-operator-runtime-semantics-evaluation
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands:
     *   Stack: obj, key => succeeded
     */ \
    MACRO(JSOP_DELELEM, "delelem", NULL, 1, 2, 1, JOF_BYTE|JOF_ELEM|JOF_CHECKSLOPPY) \
    /*
     * Like `JSOP_DELELEM, but for strict mode code. Push `true` on success,
     * else throw a TypeError.
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands:
     *   Stack: obj, key => succeeded
     */ \
    MACRO(JSOP_STRICTDELELEM, "strict-delelem", NULL, 1, 2, 1, JOF_BYTE|JOF_ELEM|JOF_CHECKSTRICT) \
    /*
     * Push true if `obj` has an own property `id`.
     *
     * Note that `obj` is the top value, like `JSOP_IN`.
     *
     * This opcode is not used for normal JS. Self-hosted code uses it by
     * calling the intrinsic `hasOwn(id, obj)`. For example,
     * `Object.prototype.hasOwnProperty` is implemented this way (see
     * js/src/builtin/Object.js).
     *
     *   Category: Objects
     *   Type: Accessing properties
     *   Operands:
     *   Stack: id, obj => (obj.hasOwnProperty(id))
     */ \
    MACRO(JSOP_HASOWN, "hasown", NULL, 1, 2, 1, JOF_BYTE|JOF_IC) \
    /*
     * Push the SuperBase of the method `callee`. The SuperBase is
     * `callee.[[HomeObject]].[[GetPrototypeOf]]()`, the object where `super`
     * property lookups should begin.
     *
     * `callee` must be a function that has a HomeObject that's an object,
     * typically produced by `JSOP_CALLEE` or `JSOP_ENVCALLEE`.
     *
     * Implements: [GetSuperBase][1], except that instead of the environment,
     * the argument supplies the callee.
     *
     * [1]: https://tc39.es/ecma262/#sec-getsuperbase
     *
     *   Category: Objects
     *   Type: Super
     *   Operands:
     *   Stack: callee => superBase
     */ \
    MACRO(JSOP_SUPERBASE, "superbase", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Get the value of `receiver.name`, starting the property search at `obj`.
     * In spec terms, `obj.[[Get]](name, receiver)`.
     *
     * Implements: [GetValue][1] for references created by [`super.name`][2].
     * The `receiver` is `this` and `obj` is the SuperBase of the enclosing
     * method.
     *
     * [1]: https://tc39.es/ecma262/#sec-getvalue
     * [2]: https://tc39.es/ecma262/#sec-super-keyword-runtime-semantics-evaluation
     *
     *   Category: Objects
     *   Type: Super
     *   Operands: uint32_t nameIndex
     *   Stack: receiver, obj => super.name
     */ \
    MACRO(JSOP_GETPROP_SUPER, "getprop-super", NULL, 5, 2, 1, JOF_ATOM|JOF_PROP|JOF_TYPESET|JOF_IC) \
    /*
     * Get the value of `receiver[key]`, starting the property search at `obj`.
     * In spec terms, `obj.[[Get]](key, receiver)`.
     *
     * Implements: [GetValue][1] for references created by [`super[key]`][2]
     * (where the `receiver` is `this` and `obj` is the SuperBase of the enclosing
     * method); [`Reflect.get(obj, key, receiver)`][3].
     *
     * [1]: https://tc39.es/ecma262/#sec-getvalue
     * [2]: https://tc39.es/ecma262/#sec-super-keyword-runtime-semantics-evaluation
     * [3]: https://tc39.es/ecma262/#sec-reflect.get
     *
     *   Category: Objects
     *   Type: Super
     *   Operands:
     *   Stack: receiver, key, obj => super[key]
     */ \
    MACRO(JSOP_GETELEM_SUPER, "getelem-super", NULL, 1, 3, 1, JOF_BYTE|JOF_ELEM|JOF_TYPESET|JOF_IC) \
    /*
     * Assign `val` to `receiver.name`, starting the search for an existing
     * property at `obj`. In spec terms, `obj.[[Set]](name, val, receiver)`.
     *
     * Implements: [PutValue][1] for references created by [`super.name`][2] in
     * non-strict code. The `receiver` is `this` and `obj` is the SuperBase of
     * the enclosing method.
     *
     * [1]: https://tc39.es/ecma262/#sec-putvalue
     * [2]: https://tc39.es/ecma262/#sec-super-keyword-runtime-semantics-evaluation
     *
     *   Category: Objects
     *   Type: Super
     *   Operands: uint32_t nameIndex
     *   Stack: receiver, obj, val => val
     */ \
    MACRO(JSOP_SETPROP_SUPER, "setprop-super", NULL, 5, 3, 1, JOF_ATOM|JOF_PROP|JOF_PROPSET|JOF_DETECTING|JOF_CHECKSLOPPY) \
    /*
     * Like `JSOP_SETPROP_SUPER`, but for strict mode code.
     *
     *   Category: Objects
     *   Type: Super
     *   Operands: uint32_t nameIndex
     *   Stack: receiver, obj, val => val
     */ \
    MACRO(JSOP_STRICTSETPROP_SUPER, "strictsetprop-super", NULL, 5, 3, 1, JOF_ATOM|JOF_PROP|JOF_PROPSET|JOF_DETECTING|JOF_CHECKSTRICT) \
    /*
     * Assign `val` to `receiver[key]`, strating the search for an existing
     * property at `obj`. In spec terms, `obj.[[Set]](key, val, receiver)`.
     *
     * Implements: [PutValue][1] for references created by [`super[key]`][2] in
     * non-strict code. The `receiver` is `this` and `obj` is the SuperBase of
     * the enclosing method.
     *
     * [1]: https://tc39.es/ecma262/#sec-putvalue
     * [2]: https://tc39.es/ecma262/#sec-super-keyword-runtime-semantics-evaluation
     *
     *   Category: Objects
     *   Type: Super
     *   Operands:
     *   Stack: receiver, key, obj, val => val
     */ \
    MACRO(JSOP_SETELEM_SUPER, "setelem-super", NULL, 1, 4, 1, JOF_BYTE|JOF_ELEM|JOF_PROPSET|JOF_DETECTING|JOF_CHECKSLOPPY) \
    /*
     * Like `JSOP_SETELEM_SUPER`, but for strict mode code.
     *
     *   Category: Objects
     *   Type: Super
     *   Operands:
     *   Stack: receiver, key, obj, val => val
     */ \
    MACRO(JSOP_STRICTSETELEM_SUPER, "strict-setelem-super", NULL, 1, 4, 1, JOF_BYTE|JOF_ELEM|JOF_PROPSET|JOF_DETECTING|JOF_CHECKSTRICT) \
    /*
     * Set up a for-in loop by pushing a `PropertyIteratorObject` over the
     * enumerable properties of `val`.
     *
     * Implements: [ForIn/OfHeadEvaluation][1] step 6,
     * [EnumerateObjectProperties][1]. (The spec refers to an "Iterator object"
     * with a `next` method, but notes that it "is never directly accessible"
     * to scripts. The object we use for this has no public methods.)
     *
     * If `val` is null or undefined, this pushes an empty iterator.
     *
     * The `iter` object pushed by this instruction must not be used or removed
     * from the stack except by `JSOP_MOREITER` and `JSOP_ENDITER`, or by error
     * handling.
     *
     * The script's `JSScript::trynotes()` must mark the body of the `for-in`
     * loop, i.e. exactly those instructions that begin executing with `iter`
     * on the stack, starting with the next instruction (always
     * `JSOP_LOOPHEAD`). Code must not jump into or out of this region: control
     * can enter only by executing `JSOP_ITER` and can exit only by executing a
     * `JSOP_ENDITER` or by exception unwinding. (A `JSOP_ENDITER` is always
     * emitted at the end of the loop, and extra copies are emitted on "exit
     * slides", where a `break`, `continue`, or `return` statement exits the
     * loop.)
     *
     * Typically a single try note entry marks the contiguous chunk of bytecode
     * from the instruction after `JSOP_ITER` to `JSOP_ENDITER` (inclusive);
     * but if that range contains any instructions on exit slides, after a
     * `JSOP_ENDITER`, then those must be correctly noted as *outside* the
     * loop.
     *
     * [1]: https://tc39.es/ecma262/#sec-runtime-semantics-forin-div-ofheadevaluation-tdznames-expr-iterationkind
     * [2]: https://tc39.es/ecma262/#sec-enumerate-object-properties
     *
     *   Category: Objects
     *   Type: Enumeration
     *   Operands:
     *   Stack: val => iter
     */ \
    MACRO(JSOP_ITER, "iter", NULL, 1, 1, 1, JOF_BYTE|JOF_IC) \
    /*
     * Get the next property name for a for-in loop.
     *
     * `iter` must be a `PropertyIteratorObject` produced by `JSOP_ITER`.  This
     * pushes the property name for the next loop iteration, or
     * `MagicValue(JS_NO_ITER_VALUE)` if there are no more enumerable
     * properties to iterate over. The magic value must be used only by
     * `JSOP_ISNOITER` and `JSOP_ENDITER`.
     *
     *   Category: Objects
     *   Type: Enumeration
     *   Operands:
     *   Stack: iter => iter, name
     */ \
    MACRO(JSOP_MOREITER, "moreiter", NULL, 1, 1, 2, JOF_BYTE) \
    /*
     * Test whether the value on top of the stack is
     * `MagicValue(JS_NO_ITER_VALUE)` and push the boolean result.
     *
     *   Category: Objects
     *   Type: Enumeration
     *   Operands:
     *   Stack: val => val, done
     */ \
    MACRO(JSOP_ISNOITER, "isnoiter", NULL, 1, 1, 2, JOF_BYTE) \
    /*
     * No-op instruction to hint to IonBuilder that the value on top of the
     * stack is the (likely string) key in a for-in loop.
     *
     *   Category: Objects
     *   Type: Enumeration
     *   Operands:
     *   Stack: val => val
     */ \
    MACRO(JSOP_ITERNEXT, "iternext", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Exit a for-in loop, closing the iterator.
     *
     * `iter` must be a `PropertyIteratorObject` pushed by `JSOP_ITER`.
     *
     *   Category: Objects
     *   Type: Enumeration
     *   Operands:
     *   Stack: iter, iterval =>
     */ \
    MACRO(JSOP_ENDITER, "enditer", NULL, 1, 2, 0, JOF_BYTE) \
    /*
     * Check that the top value on the stack is an object, and throw a
     * TypeError if not. `kind` is used only to generate an appropriate error
     * message. It must be in range for `js::CheckIsObjectKind`.
     *
     * Implements: [GetIterator][1] step 5, [IteratorNext][2] step 3. Both
     * operations call a JS method which scripts can define however they want,
     * so they check afterwards that the method returned an object.
     *
     * [1]: https://tc39.es/ecma262/#sec-getiterator
     * [2]: https://tc39.es/ecma262/#sec-iteratornext
     *
     *   Category: Objects
     *   Type: Iteration
     *   Operands: uint8_t kind
     *   Stack: result => result
     */ \
    MACRO(JSOP_CHECKISOBJ, "checkisobj", NULL, 2, 1, 1, JOF_UINT8) \
    /*
     * Check that the top value on the stack is callable, and throw a TypeError
     * if not. The operand `kind` is used only to generate an appropriate error
     * message. It must be in range for `js::CheckIsCallableKind`.
     *
     *   Category: Objects
     *   Type: Iteration
     *   Operands: uint8_t kind
     *   Stack: obj => obj
     */ \
    MACRO(JSOP_CHECKISCALLABLE, "checkiscallable", NULL, 2, 1, 1, JOF_UINT8) \
    /*
     * Throw a TypeError if `val` is `null` or `undefined`.
     *
     * Implements: [RequireObjectCoercible][1]. But most instructions that
     * require an object will perform this check for us, so of the dozens of
     * calls to RequireObjectCoercible in the spec, we need this instruction
     * only for [destructuring assignment][2] and [initialization][3].
     *
     * [1]: https://tc39.es/ecma262/#sec-requireobjectcoercible
     * [2]: https://tc39.es/ecma262/#sec-runtime-semantics-destructuringassignmentevaluation
     * [3]: https://tc39.es/ecma262/#sec-destructuring-binding-patterns-runtime-semantics-bindinginitialization
     *
     *   Category: Objects
     *   Type: Iteration
     *   Operands:
     *   Stack: val => val
     */ \
    MACRO(JSOP_CHECKOBJCOERCIBLE, "checkobjcoercible", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Create and push an async iterator wrapping the sync iterator `iter`.
     * `next` should be `iter`'s `.next` method.
     *
     * Implements: [CreateAsyncToSyncIterator][1]. The spec says this operation
     * takes one argument, but that argument is a Record with two relevant
     * fields, `[[Iterator]]` and `[[NextMethod]]`.
     *
     * Used for `for await` loops.
     *
     * [1]: https://tc39.es/ecma262/#sec-createasyncfromsynciterator
     *
     *   Category: Objects
     *   Type: Iteration
     *   Operands:
     *   Stack: iter, next => asynciter
     */ \
    MACRO(JSOP_TOASYNCITER, "toasynciter", NULL, 1, 2, 1, JOF_BYTE) \
    /*
     * Set the prototype of `obj`.
     *
     * `obj` must be an object.
     *
     * Implements: [B.3.1 __proto__ Property Names in Object Initializers][1], step 7.a.
     *
     * [1]: https://tc39.es/ecma262/#sec-__proto__-property-names-in-object-initializers
     *
     *   Category: Objects
     *   Type: SetPrototype
     *   Operands:
     *   Stack: obj, protoVal => obj
     */ \
    MACRO(JSOP_MUTATEPROTO, "mutateproto", NULL, 1, 2, 1, JOF_BYTE) \
    /*
     * Create and push a new Array object with the given `length`,
     * preallocating enough memory to hold that many elements.
     *
     *   Category: Objects
     *   Type: Array literals
     *   Operands: uint32_t length
     *   Stack: => array
     */ \
    MACRO(JSOP_NEWARRAY, "newarray", NULL, 5, 0, 1, JOF_UINT32|JOF_IC) \
    /*
     * Initialize an array element `array[index]` with value `val`.
     *
     * `val` may be `MagicValue(JS_ELEMENTS_HOLE)`. If it is, this does nothing.
     *
     * This never calls setters or proxy traps.
     *
     * `array` must be an Array object created by `JSOP_NEWARRAY` with length >
     * `index`, and never used except by `JSOP_INITELEM_ARRAY`.
     *
     * Implements: [ArrayAccumulation][1], the third algorithm, step 4, in the
     * common case where *nextIndex* is known.
     *
     * [1]: https://tc39.es/ecma262/#sec-runtime-semantics-arrayaccumulation
     *
     *   Category: Objects
     *   Type: Array literals
     *   Operands: uint32_t index
     *   Stack: array, val => array
     */ \
    MACRO(JSOP_INITELEM_ARRAY, "initelem_array", NULL, 5, 2, 1, JOF_UINT32|JOF_ELEM|JOF_PROPINIT|JOF_DETECTING|JOF_IC) \
    /*
     * Initialize an array element `array[index++]` with value `val`.
     *
     * `val` may be `MagicValue(JS_ELEMENTS_HOLE)`. If it is, no element is
     * defined, but the array length and the stack value `index` are still
     * incremented.
     *
     * This never calls setters or proxy traps.
     *
     * `array` must be an Array object created by `JSOP_NEWARRAY` and never used
     * except by `JSOP_INITELEM_ARRAY` and `JSOP_INITELEM_INC`.
     *
     * `index` must be an integer, `0 <= index <= INT32_MAX`. If `index` is
     * `INT32_MAX`, this throws a RangeError.
     *
     * This instruction is used when an array literal contains a
     * *SpreadElement*. In `[a, ...b, c]`, `INITELEM_ARRAY 0` is used to put
     * `a` into the array, but `INITELEM_INC` is used for the elements of `b`
     * and for `c`.
     *
     * Implements: Several steps in [ArrayAccumulation][1] that call
     * CreateDataProperty, set the array length, and/or increment *nextIndex*.
     *
     * [1]: https://tc39.es/ecma262/#sec-runtime-semantics-arrayaccumulation
     *
     *   Category: Objects
     *   Type: Array literals
     *   Operands:
     *   Stack: array, index, val => array, (index + 1)
     */ \
    MACRO(JSOP_INITELEM_INC, "initelem_inc", NULL, 1, 3, 2, JOF_BYTE|JOF_ELEM|JOF_PROPINIT|JOF_IC) \
    /*
     * Push `MagicValue(JS_ELEMENTS_HOLE)`, representing an *Elision* in an
     * array literal (like the missing property 0 in the array `[, 1]`).
     *
     * This magic value must be used only by `JSOP_INITELEM_ARRAY` or
     * `JSOP_INITELEM_INC`.
     *
     *   Category: Objects
     *   Type: Array literals
     *   Operands:
     *   Stack: => hole
     */ \
    MACRO(JSOP_HOLE, "hole", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Create and push a new array that shares the elements of a template
     * object.
     *
     * `script->getObject(objectIndex)` must be a copy-on-write array whose
     * elements are all primitive values.
     *
     * This is an optimization. This single instruction implements an entire
     * array literal, saving run time, code, and memory compared to
     * `JSOP_NEWARRAY` and a series of `JSOP_INITELEM` instructions.
     *
     *   Category: Objects
     *   Type: Array literals
     *   Operands: uint32_t objectIndex
     *   Stack: => array
     */ \
    MACRO(JSOP_NEWARRAY_COPYONWRITE, "newarray_copyonwrite", NULL, 5, 0, 1, JOF_OBJECT) \
    /*
     * Clone and push a new RegExp object.
     *
     * Implements: [Evaluation for *RegularExpressionLiteral*][1].
     *
     * [1]: https://tc39.es/ecma262/#sec-regular-expression-literals-runtime-semantics-evaluation
     *
     *   Category: Objects
     *   Type: RegExp literals
     *   Operands: uint32_t regexpIndex
     *   Stack: => regexp
     */ \
    MACRO(JSOP_REGEXP, "regexp", NULL, 5, 0, 1, JOF_REGEXP) \
    /*
     * Push a function object.
     *
     * This clones the function unless it's a singleton; see
     * `CanReuseFunctionForClone`. The new function inherits the current
     * environment chain.
     *
     * Used to create most JS functions. Notable exceptions are arrow functions
     * and derived or default class constructors.
     *
     * The function indicated by `funcIndex` must be a non-arrow function.
     *
     * Implements: [InstantiateFunctionObject][1], [Evaluation for
     * *FunctionExpression*][2], and so on.
     *
     * [1]: https://tc39.es/ecma262/#sec-function-definitions-runtime-semantics-instantiatefunctionobject
     * [2]: https://tc39.es/ecma262/#sec-function-definitions-runtime-semantics-evaluation
     *
     *   Category: Functions
     *   Type: Creating functions
     *   Operands: uint32_t funcIndex
     *   Stack: => fn
     */ \
    MACRO(JSOP_LAMBDA, "lambda", NULL, 5, 0, 1, JOF_OBJECT) \
    /*
     * Push a new arrow function.
     *
     * `newTarget` matters only if the arrow function uses the expression
     * `new.target`. It should be the current value of `new.target`, so that
     * the arrow function inherits `new.target` from the enclosing scope. (If
     * `new.target` is illegal here, the value doesn't matter; use `null`.)
     *
     * The function indicated by `funcIndex` must be an arrow function.
     *
     *   Category: Functions
     *   Type: Creating functions
     *   Operands: uint32_t funcIndex
     *   Stack: newTarget => arrowFn
     */ \
    MACRO(JSOP_LAMBDA_ARROW, "lambda_arrow", NULL, 5, 1, 1, JOF_OBJECT) \
    /*
     * Set the name of a function.
     *
     * `fun` must be a function object. `name` must be a string, Int32 value,
     * or symbol (like the result of `JSOP_TOID`).
     *
     * Implements: [SetFunctionName][1], used e.g. to name methods with
     * computed property names.
     *
     * [1]: https://tc39.es/ecma262/#sec-setfunctionname
     *
     *   Category: Functions
     *   Type: Creating functions
     *   Operands: uint8_t prefixKind
     *   Stack: fun, name => fun
     */ \
    MACRO(JSOP_SETFUNNAME, "setfunname", NULL, 2, 2, 1, JOF_UINT8) \
    /*
     * Initialize the home object for functions with super bindings.
     *
     *   Category: Functions
     *   Type: Creating functions
     *   Operands:
     *   Stack: fun, homeObject => fun
     */ \
    MACRO(JSOP_INITHOMEOBJECT, "inithomeobject", NULL, 1, 2, 1, JOF_BYTE) \
    /*
     * Throw a TypeError if `baseClass` isn't either `null` or a constructor.
     *
     * Implements: [ClassDefinitionEvaluation][1] step 6.f.
     *
     * [1]: https://tc39.es/ecma262/#sec-runtime-semantics-classdefinitionevaluation
     *
     *   Category: Functions
     *   Type: Creating constructors
     *   Operands:
     *   Stack: baseClass => baseClass
     */ \
    MACRO(JSOP_CHECKCLASSHERITAGE, "checkclassheritage", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Like `JSOP_LAMBDA`, but using `proto` as the new function's
     * `[[Prototype]]` (or `%FunctionPrototype%` if `proto` is `null`).
     *
     * `proto` must be either a constructor or `null`. We use
     * `JSOP_CHECKCLASSHERITAGE` to check.
     *
     * This is used to create the constructor for a derived class.
     *
     * Implements: [ClassDefinitionEvaluation][1] steps 6.e.ii, 6.g.iii, and
     * 12 for derived classes.
     *
     * [1]: https://tc39.es/ecma262/#sec-runtime-semantics-classdefinitionevaluation
     *
     *   Category: Functions
     *   Type: Creating constructors
     *   Operands: uint32_t funcIndex
     *   Stack: proto => obj
     */ \
    MACRO(JSOP_FUNWITHPROTO, "funwithproto", NULL, 5, 1, 1, JOF_OBJECT) \
    /*
     * Create and push a default constructor for a base class.
     *
     * A default constructor behaves like `constructor() {}`.
     *
     * Implements: [ClassDefinitionEvaluation for *ClassTail*][1], steps
     * 10.b. and 12-17.
     *
     * The `sourceStart`/`sourceEnd` offsets are the start/end offsets of the
     * class definition in the source buffer and are used for `toString()`.
     *
     * [1]: https://tc39.es/ecma262/#sec-runtime-semantics-classdefinitionevaluation
     *
     *   Category: Functions
     *   Type: Creating constructors
     *   Operands: uint32_t nameIndex, uint32_t sourceStart, uint32_t sourceEnd
     *   Stack: => constructor
     */ \
    MACRO(JSOP_CLASSCONSTRUCTOR, "classconstructor", NULL, 13, 0, 1, JOF_CLASS_CTOR) \
    /*
     * Create and push a default constructor for a derived class.
     *
     * A default derived-class constructor behaves like
     * `constructor(...args) { super(...args); }`.
     *
     * Implements: [ClassDefinitionEvaluation for *ClassTail*][1], steps
     * 10.a. and 12-17.
     *
     * The `sourceStart`/`sourceEnd` offsets are the start/end offsets of the
     * class definition in the source buffer and are used for `toString()`.
     *
     * [1]: https://tc39.es/ecma262/#sec-runtime-semantics-classdefinitionevaluation
     *
     *   Category: Functions
     *   Type: Creating constructors
     *   Operands: uint32_t nameIndex, uint32_t sourceStart, uint32_t sourceEnd
     *   Stack: proto => constructor
     */ \
    MACRO(JSOP_DERIVEDCONSTRUCTOR, "derivedconstructor", NULL, 13, 1, 1, JOF_CLASS_CTOR) \
    /*
     * Pushes the current global's builtin prototype for a given proto key.
     *
     *   Category: Functions
     *   Type: Creating constructors
     *   Operands: uint8_t kind
     *   Stack: => %BuiltinPrototype%
     */ \
    MACRO(JSOP_BUILTINPROTO, "builtinproto", NULL, 2, 0, 1, JOF_UINT8) \
    /*
     * Invoke `callee` with `this` and `args`, and push the return value. Throw
     * a TypeError if `callee` isn't a function.
     *
     * `JSOP_CALLITER` is used for implicit calls to @@iterator methods, to
     * ensure error messages are formatted with `JSMSG_NOT_ITERABLE` ("x is not
     * iterable") rather than `JSMSG_NOT_FUNCTION` ("x[Symbol.iterator] is not
     * a function"). The `argc` operand must be 0 for this variation.
     *
     * `JSOP_FUNAPPLY` hints to the VM that this is likely a call to the
     * builtin method `Function.prototype.apply`, an easy optimization target.
     *
     * `JSOP_FUNCALL` similarly hints to the VM that the callee is likely
     * `Function.prototype.call`.
     *
     * `JSOP_CALL_IGNORES_RV` hints to the VM that the return value is ignored.
     * This allows alternate faster implementations to be used that avoid
     * unnecesary allocations.
     *
     * Implements: [EvaluateCall][1] steps 4, 5, and 7.
     *
     * [1]: https://tc39.es/ecma262/#sec-evaluatecall
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands: uint16_t argc
     *   Stack: callee, this, args[0], ..., args[argc-1] => rval
     *   nuses: (argc+2)
     */ \
    MACRO(JSOP_CALL, "call", NULL, 3, -1, 1, JOF_ARGC|JOF_INVOKE|JOF_TYPESET|JOF_IC) \
    MACRO(JSOP_CALLITER, "calliter", NULL, 3, -1, 1, JOF_ARGC|JOF_INVOKE|JOF_TYPESET|JOF_IC) \
    MACRO(JSOP_FUNAPPLY, "funapply", NULL, 3, -1, 1, JOF_ARGC|JOF_INVOKE|JOF_TYPESET|JOF_IC) \
    MACRO(JSOP_FUNCALL, "funcall", NULL, 3, -1, 1, JOF_ARGC|JOF_INVOKE|JOF_TYPESET|JOF_IC) \
    MACRO(JSOP_CALL_IGNORES_RV, "call-ignores-rv", NULL, 3, -1, 1, JOF_ARGC|JOF_INVOKE|JOF_TYPESET|JOF_IC) \
    /*
     * Like `JSOP_CALL`, but the arguments are provided in an array rather than
     * a span of stack slots. Used to implement spread-call syntax:
     * `f(...args)`.
     *
     * `args` must be an Array object containing the actual arguments. The
     * array must be packed (dense and free of holes; see IsPackedArray).
     * This can be ensured by creating the array with `JSOP_NEWARRAY` and
     * populating it using `JSOP_INITELEM_ARRAY`.
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands:
     *   Stack: callee, this, args => rval
     */ \
    MACRO(JSOP_SPREADCALL, "spreadcall", NULL, 1, 3, 1, JOF_BYTE|JOF_INVOKE|JOF_TYPESET|JOF_IC) \
    /*
     * Push true if `arr` is an array object that can be passed directly as the
     * `args` argument to `JSOP_SPREADCALL`.
     *
     * This instruction and the branch around the iterator loop are emitted
     * only when `arr` is itself a rest parameter, as in `(...arr) =>
     * f(...arr)`, a strong hint that it's a packed Array whose prototype is
     * `Array.prototype`.
     *
     * See `js::OptimizeSpreadCall`.
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands:
     *   Stack: arr => arr, optimized
     */ \
    MACRO(JSOP_OPTIMIZE_SPREADCALL, "optimize-spreadcall", NULL, 1, 1, 2, JOF_BYTE) \
    /*
     * Perform a direct eval in the current environment if `callee` is the
     * builtin `eval` function, otherwise follow same behaviour as `JSOP_CALL`.
     *
     * All direct evals use one of the JSOP_*EVAL operations here and these
     * opcodes are only used when the syntactic conditions for a direct eval
     * are met. If the builtin `eval` function is called though other means, it
     * becomes an indirect eval.
     *
     * Direct eval causes all bindings in *enclosing* non-global scopes to be
     * marked "aliased". The optimization that puts bindings in stack slots has
     * to prove that the bindings won't need to be captured by closures or
     * accessed using `JSOP_{GET,BIND,SET,DEL}NAME` instructions. Direct eval
     * makes that analysis impossible.
     *
     * Implements: [Function Call Evaluation][1], steps 5-7 and 9, when the
     * syntactic critera for direct eval in step 6 are all met.
     *
     * [1]: https://tc39.es/ecma262/#sec-function-calls-runtime-semantics-evaluation
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands: uint16_t argc
     *   Stack: callee, this, args[0], ..., args[argc-1] => rval
     *   nuses: (argc+2)
     */ \
    MACRO(JSOP_EVAL, "eval", NULL, 3, -1, 1, JOF_ARGC|JOF_INVOKE|JOF_TYPESET|JOF_CHECKSLOPPY|JOF_IC) \
    /*
     * Spread-call variant of `JSOP_EVAL`.
     *
     * See `JSOP_SPREADCALL` for restrictions on `args`.
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands:
     *   Stack: callee, this, args => rval
     */ \
    MACRO(JSOP_SPREADEVAL, "spreadeval", NULL, 1, 3, 1, JOF_BYTE|JOF_INVOKE|JOF_TYPESET|JOF_CHECKSLOPPY|JOF_IC) \
    /*
     * Like `JSOP_EVAL`, but for strict mode code.
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands: uint16_t argc
     *   Stack: evalFn, this, args[0], ..., args[argc-1] => rval
     *   nuses: (argc+2)
     */ \
    MACRO(JSOP_STRICTEVAL, "strict-eval", NULL, 3, -1, 1, JOF_ARGC|JOF_INVOKE|JOF_TYPESET|JOF_CHECKSTRICT|JOF_IC) \
    /*
     * Spread-call variant of `JSOP_STRICTEVAL`.
     *
     * See `JSOP_SPREADCALL` for restrictions on `args`.
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands:
     *   Stack: callee, this, args => rval
     */ \
    MACRO(JSOP_STRICTSPREADEVAL, "strict-spreadeval", NULL, 1, 3, 1, JOF_BYTE|JOF_INVOKE|JOF_TYPESET|JOF_CHECKSTRICT|JOF_IC) \
    /*
     * Push the implicit `this` value for an unqualified function call, like
     * `foo()`. `nameIndex` gives the name of the function we're calling.
     *
     * The result is always `undefined` except when the name refers to a `with`
     * binding.  For example, in `with (date) { getFullYear(); }`, the
     * implicit `this` passed to `getFullYear` is `date`, not `undefined`.
     *
     * This walks the run-time environment chain looking for the environment
     * record that contains the function. If the function call is not inside a
     * `with` statement, use `JSOP_GIMPLICITTHIS` instead. If the function call
     * definitely refers to a local binding, use `JSOP_UNDEFINED`.
     *
     * Implements: [EvaluateCall][1] step 1.b. But not entirely correctly.
     * See [bug 1166408][2].
     *
     * [1]: https://tc39.es/ecma262/#sec-evaluatecall
     * [2]: https://bugzilla.mozilla.org/show_bug.cgi?id=1166408
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands: uint32_t nameIndex
     *   Stack: => this
     */ \
    MACRO(JSOP_IMPLICITTHIS, "implicitthis", "", 5, 0, 1, JOF_ATOM) \
    /*
     * Like `JSOP_IMPLICITTHIS`, but the name must not be bound in any local
     * environments.
     *
     * The result is always `undefined` except when the name refers to a
     * binding in a non-syntactic `with` environment.
     *
     * Note: The frontend has to emit `JSOP_GIMPLICITTHIS` (and not
     * `JSOP_UNDEFINED`) for global unqualified function calls, even when
     * `CompileOptions::nonSyntacticScope == false`, because later
     * `js::CloneGlobalScript` can be called with `ScopeKind::NonSyntactic` to
     * clone the script into a non-syntactic environment, with the bytecode
     * reused, unchanged.
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands: uint32_t nameIndex
     *   Stack: => this
     */ \
    MACRO(JSOP_GIMPLICITTHIS, "gimplicitthis", "", 5, 0, 1, JOF_ATOM) \
    /*
     * Push the call site object for a tagged template call.
     *
     * `script->getObject(objectIndex)` is the call site object;
     * `script->getObject(objectIndex + 1)` is the raw object.
     *
     * The first time this instruction runs for a given template, it assembles
     * the final value, defining the `.raw` property on the call site object
     * and freezing both objects.
     *
     * Implements: [GetTemplateObject][1], steps 4 and 12-16.
     *
     * [1]: https://tc39.es/ecma262/#sec-gettemplateobject
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands: uint32_t objectIndex
     *   Stack: => callSiteObj
     */ \
    MACRO(JSOP_CALLSITEOBJ, "callsiteobj", NULL, 5, 0, 1, JOF_OBJECT) \
    /*
     * Push `MagicValue(JS_IS_CONSTRUCTING)`.
     *
     * This magic value is a required argument to the `JSOP_NEW` and
     * `JSOP_SUPERCALL` instructions and must not be used any other way.
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands:
     *   Stack: => JS_IS_CONSTRUCTING
     */ \
    MACRO(JSOP_IS_CONSTRUCTING, "is-constructing", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Invoke `callee` as a constructor with `args` and `newTarget`, and push
     * the return value. Throw a TypeError if `callee` isn't a constructor.
     *
     * `isConstructing` must be the value pushed by `JSOP_IS_CONSTRUCTING`.
     *
     * `JSOP_SUPERCALL` behaves exactly like `JSOP_NEW`, but is used for
     * *SuperCall* expressions, to allow JITs to distinguish them from `new`
     * expressions.
     *
     * Implements: [EvaluateConstruct][1] steps 7 and 8.
     *
     * [1]: https://tc39.es/ecma262/#sec-evaluatenew
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands: uint16_t argc
     *   Stack: callee, isConstructing, args[0], ..., args[argc-1], newTarget => rval
     *   nuses: (argc+3)
     */ \
    MACRO(JSOP_NEW, "new", NULL, 3, -1, 1, JOF_ARGC|JOF_INVOKE|JOF_TYPESET|JOF_IC|JOF_IC) \
    MACRO(JSOP_SUPERCALL, "supercall", NULL, 3, -1, 1, JOF_ARGC|JOF_INVOKE|JOF_TYPESET|JOF_IC) \
    /*
     * Spread-call variant of `JSOP_NEW`.
     *
     * Invokes `callee` as a constructor with `args` and `newTarget`, and
     * pushes the return value onto the stack.
     *
     * `isConstructing` must be the value pushed by `JSOP_IS_CONSTRUCTING`.
     * See `JSOP_SPREADCALL` for restrictions on `args`.
     *
     * `JSOP_SPREADSUPERCALL` behaves exactly like `JSOP_SPREADNEW`, but is
     * used for *SuperCall* expressions.
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands:
     *   Stack: callee, isConstructing, args, newTarget => rval
     */ \
    MACRO(JSOP_SPREADNEW, "spreadnew", NULL, 1, 4, 1, JOF_BYTE|JOF_INVOKE|JOF_TYPESET|JOF_IC) \
    MACRO(JSOP_SPREADSUPERCALL, "spreadsupercall", NULL, 1, 4, 1, JOF_BYTE|JOF_INVOKE|JOF_TYPESET|JOF_IC) \
    /*
     * Push the prototype of `callee` in preparation for calling `super()`.
     * Throw a TypeError if that value is not a constructor.
     *
     * `callee` must be a derived class constructor.
     *
     * Implements: [GetSuperConstructor][1], steps 4-7.
     *
     * [1]: https://tc39.es/ecma262/#sec-getsuperconstructor
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands:
     *   Stack: callee => superFun
     */ \
    MACRO(JSOP_SUPERFUN, "superfun", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Throw a ReferenceError if `thisval` is not
     * `MagicValue(JS_UNINITIALIZED_LEXICAL)`. Used in derived class
     * constructors to prohibit calling `super` more than once.
     *
     * Implements: [BindThisValue][1], step 3.
     *
     * [1]: https://tc39.es/ecma262/#sec-bindthisvalue
     *
     *   Category: Functions
     *   Type: Calls
     *   Operands:
     *   Stack: thisval => thisval
     */ \
    MACRO(JSOP_CHECKTHISREINIT, "checkthisreinit", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Initializes generator frame, creates a generator and pushes it on the
     * stack.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands:
     *   Stack: => generator
     */ \
    MACRO(JSOP_GENERATOR, "generator", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Pops the generator from the top of the stack, suspends it and stops
     * execution.
     *
     * When resuming execution, JSOP_RESUME pushes the rval, gen and resumeKind
     * values. resumeKind is the GeneratorResumeKind stored as int32.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands: uint24_t resumeIndex
     *   Stack: gen => rval, gen, resumeKind
     */ \
    MACRO(JSOP_INITIALYIELD, "initialyield", NULL, 4, 1, 3, JOF_RESUMEINDEX) \
    /*
     * Bytecode emitted after 'yield' expressions. This is useful for the
     * Debugger and `AbstractGeneratorObject::isAfterYieldOrAwait`. It's
     * treated as jump target op so that the Baseline Interpreter can
     * efficiently restore the frame's interpreterICEntry when resuming a
     * generator.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands: uint32_t icIndex
     *   Stack: =>
     */ \
    MACRO(JSOP_AFTERYIELD, "afteryield", NULL, 5, 0, 0, JOF_ICINDEX) \
    /*
     * Pops the generator and suspends and closes it. Yields the value in the
     * frame's return value slot.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands:
     *   Stack: gen =>
     */ \
    MACRO(JSOP_FINALYIELDRVAL, "finalyieldrval", NULL, 1, 1, 0, JOF_BYTE) \
    /*
     * Pops the generator and the return value 'rval1', stops execution and
     * returns 'rval1'.
     *
     * When resuming execution, JSOP_RESUME pushes the rval2, gen and resumeKind
     * values.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands: uint24_t resumeIndex
     *   Stack: rval1, gen => rval2, gen, resumeKind
     */ \
    MACRO(JSOP_YIELD, "yield", NULL, 4, 2, 3, JOF_RESUMEINDEX) \
    /*
     * Pushes a boolean indicating whether the top of the stack is
     * `MagicValue(JS_GENERATOR_CLOSING)`.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands:
     *   Stack: val => val, res
     */ \
    MACRO(JSOP_ISGENCLOSING, "isgenclosing", NULL, 1, 1, 2, JOF_BYTE) \
    /*
     * Pops the top two values 'value' and 'gen' from the stack, then starts
     * "awaiting" for 'value' to be resolved, which will then resume the
     * execution of 'gen'. Pushes the async function promise on the stack, so
     * that it'll be returned to the caller on the very first "await".
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands:
     *   Stack: value, gen => promise
     */ \
    MACRO(JSOP_ASYNCAWAIT, "async-await", NULL, 1, 2, 1, JOF_BYTE) \
    /*
     * Pops the top two values 'valueOrReason' and 'gen' from the stack, then
     * pushes the promise resolved with 'valueOrReason'. `gen` must be the
     * internal generator object created in async functions. The pushed promise
     * is the async function's result promise, which is stored in `gen`.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands: uint8_t fulfillOrReject
     *   Stack: valueOrReason, gen => promise
     */ \
    MACRO(JSOP_ASYNCRESOLVE, "async-resolve", NULL, 2, 2, 1, JOF_UINT8) \
    /*
     * Pops the generator and the return value 'promise', stops execution and
     * returns 'promise'.
     *
     * When resuming execution, JSOP_RESUME pushes the resolved, gen and
     * resumeKind values. resumeKind is the GeneratorResumeKind stored as int32.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands: uint24_t resumeIndex
     *   Stack: promise, gen => resolved, gen, resumeKind
     */ \
    MACRO(JSOP_AWAIT, "await", NULL, 4, 2, 3, JOF_RESUMEINDEX) \
    /*
     * Pops the top of stack value as 'value', checks if the await for 'value'
     * can be skipped. If the await operation can be skipped and the resolution
     * value for 'value' can be acquired, pushes the resolution value and
     * 'true' onto the stack. Otherwise, pushes 'value' and 'false' on the
     * stack.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands:
     *   Stack: value => value_or_resolved, canskip
     */ \
    MACRO(JSOP_TRYSKIPAWAIT, "tryskipawait", NULL, 1, 1, 2, JOF_BYTE) \
    /*
     * Pushes one of the GeneratorResumeKind values as Int32Value.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands: resumeKind (GeneratorResumeKind)
     *   Stack: => resumeKind
     */ \
    MACRO(JSOP_RESUMEKIND, "resumekind", NULL, 2, 0, 1, JOF_UINT8) \
    /*
     * Pops the generator and resumeKind values. resumeKind is the
     * GeneratorResumeKind stored as int32. If resumeKind is Next, continue
     * execution. If resumeKind is Throw or Return, these completions are
     * handled by throwing an exception. See GeneratorThrowOrReturn.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands:
     *   Stack: rval, gen, resumeKind => rval
     */ \
    MACRO(JSOP_CHECK_RESUMEKIND, "check-resumekind", NULL, 1, 3, 1, JOF_BYTE) \
    /*
     * Pops the generator, argument and resumeKind from the stack, pushes a new
     * generator frame and resumes execution of it. Pushes the return value
     * after the generator yields.
     *
     *   Category: Functions
     *   Type: Generators and async functions
     *   Operands:
     *   Stack: gen, val, resumeKind => rval
     */ \
    MACRO(JSOP_RESUME, "resume", NULL, 1, 3, 1, JOF_BYTE|JOF_INVOKE) \
    /*
     * No-op instruction marking the target of a jump instruction.
     *
     * This instruction and a few others (see `js::BytecodeIsJumpTarget`) are
     * jump target instructions. The Baseline Interpreter uses these
     * instructions to sync the frame's `interpreterICEntry` after a jump. Ion
     * uses them to find block boundaries when translating bytecode to MIR.
     *
     *   Category: Control flow
     *   Type: Jump targets
     *   Operands: uint32_t icIndex
     *   Stack: =>
     */ \
    MACRO(JSOP_JUMPTARGET, "jumptarget", NULL, 5, 0, 0, JOF_ICINDEX) \
    /*
     * Marks the target of the backwards jump for some loop.
     *
     * This is a jump target instruction (see `JSOP_JUMPTARGET`). Additionally,
     * it checks for interrupts and handles JIT tiering.
     *
     * The `depthHint` operand is a loop depth hint for Ion. It starts at 1 and
     * deeply nested loops all have the same value.
     *
     * For the convenience of the JITs, scripts must not start with this
     * instruction. See bug 1602390.
     *
     *   Category: Control flow
     *   Type: Jump targets
     *   Operands: uint32_t icIndex, uint8_t depthHint
     *   Stack: =>
     */ \
    MACRO(JSOP_LOOPHEAD, "loophead", NULL, 6, 0, 0, JOF_LOOPHEAD) \
    /*
     * Jump to a 32-bit offset from the current bytecode.
     *
     * See "Jump instructions" above for details.
     *
     *   Category: Control flow
     *   Type: Jumps
     *   Operands: int32_t offset
     *   Stack: =>
     */ \
    MACRO(JSOP_GOTO, "goto", NULL, 5, 0, 0, JOF_JUMP) \
    /*
     * If ToBoolean(`cond`) is false, jumps to a 32-bit offset from the current
     * instruction.
     *
     *   Category: Control flow
     *   Type: Jumps
     *   Operands: int32_t forwardOffset
     *   Stack: cond =>
     */ \
    MACRO(JSOP_IFEQ, "ifeq", NULL, 5, 1, 0, JOF_JUMP|JOF_DETECTING|JOF_IC) \
    /*
     * If ToBoolean(`cond`) is true, jump to a 32-bit offset from the current
     * instruction.
     *
     * `offset` may be positive or negative. This is the instruction used at the
     * end of a do-while loop to jump back to the top.
     *
     *   Category: Control flow
     *   Type: Jumps
     *   Operands: int32_t offset
     *   Stack: cond =>
     */ \
    MACRO(JSOP_IFNE, "ifne", NULL, 5, 1, 0, JOF_JUMP|JOF_IC) \
    /*
     * Short-circuit for logical AND.
     *
     * If ToBoolean(`cond`) is false, jump to a 32-bit offset from the current
     * instruction. The value remains on the stack.
     *
     *   Category: Control flow
     *   Type: Jumps
     *   Operands: int32_t forwardOffset
     *   Stack: cond => cond
     */ \
    MACRO(JSOP_AND, "and", NULL, 5, 1, 1, JOF_JUMP|JOF_DETECTING|JOF_IC) \
    /*
     * Short-circuit for logical OR.
     *
     * If ToBoolean(`cond`) is true, jump to a 32-bit offset from the current
     * instruction. The value remains on the stack.
     *
     *   Category: Control flow
     *   Type: Jumps
     *   Operands: int32_t forwardOffset
     *   Stack: cond => cond
     */ \
    MACRO(JSOP_OR, "or", NULL, 5, 1, 1, JOF_JUMP|JOF_DETECTING|JOF_IC) \
    /*
     * Short-circuiting for nullish coalescing.
     *
     * If `val` is not null or undefined, jump to a 32-bit offset from the
     * current instruction.
     *
     *   Category: Control flow
     *   Type: Jumps
     *   Operands: int32_t forwardOffset
     *   Stack: val => val
     */ \
    MACRO(JSOP_COALESCE, "coalesce", NULL, 5, 1, 1, JOF_JUMP|JOF_DETECTING) \
     /*
     * Like `JSOP_IFNE` ("jump if true"), but if the branch is taken,
     * pop and discard an additional stack value.
     *
     * This is used to implement `switch` statements when the
     * `JSOP_TABLESWITCH` optimization is not possible. The switch statement
     *
     *     switch (expr) {
     *         case A: stmt1;
     *         case B: stmt2;
     *     }
     *
     * compiles to this bytecode:
     *
     *         # dispatch code - evaluate expr, check it against each `case`,
     *         # jump to the right place in the body or to the end.
     *         <expr>
     *         dup; <A>; stricteq; case L1; jumptarget
     *         dup; <B>; stricteq; case L2; jumptarget
     *         default LE
     *
     *         # body code
     *     L1: jumptarget; <stmt1>
     *     L2: jumptarget; <stmt2>
     *     LE: jumptarget
     *
     * This opcode is weird: it's the only one whose ndefs varies depending on
     * which way a conditional branch goes. We could implement switch
     * statements using `JSOP_IFNE` and `JSOP_POP`, but that would also be
     * awkward--putting the `POP` inside the `switch` body would complicate
     * fallthrough.
     *
     *   Category: Control flow
     *   Type: Jumps
     *   Operands: int32_t forwardOffset
     *   Stack: val, cond => val (if !cond)
     */ \
    MACRO(JSOP_CASE, "case", NULL, 5, 2, 1, JOF_JUMP) \
    /*
     * Like `JSOP_GOTO`, but pop and discard an additional stack value.
     *
     * This appears after all cases for a non-optimized `switch` statement. If
     * there's a `default:` label, it jumps to that point in the body;
     * otherwise it jumps to the next statement.
     *
     *   Category: Control flow
     *   Type: Jumps
     *   Operands: int32_t forwardOffset
     *   Stack: lval =>
     */ \
    MACRO(JSOP_DEFAULT, "default", NULL, 5, 1, 0, JOF_JUMP) \
    /*
     * Optimized switch-statement dispatch, used when all `case` labels are
     * small integer constants.
     *
     * If `low <= i <= high`, jump to the instruction at the offset given by
     * `script->resumeOffsets()[firstResumeIndex + i - low]`, in bytes from the
     * start of the current script's bytecode. Otherwise, jump to the
     * instruction at `defaultOffset` from the current instruction. All of
     * these offsets must be in range for the current script and must point to
     * `JSOP_JUMPTARGET` instructions.
     *
     * The following inequalities must hold: `low <= high` and
     * `firstResumeIndex + high - low < resumeOffsets().size()`.
     *
     *   Category: Control flow
     *   Type: Jumps
     *   Operands: int32_t defaultOffset, int32_t low, int32_t high,
     *             uint24_t firstResumeIndex
     *   Stack: i =>
     */ \
    MACRO(JSOP_TABLESWITCH, "tableswitch", NULL, 16, 1, 0, JOF_TABLESWITCH|JOF_DETECTING) \
    /*
     * Return `rval`.
     *
     * This must not be used in derived class constructors. Instead use
     * `JSOP_SETRVAL`, `JSOP_CHECKRETURN`, and `JSOP_RETRVAL`.
     *
     *   Category: Control flow
     *   Type: Return
     *   Operands:
     *   Stack: rval =>
     */ \
    MACRO(JSOP_RETURN, "return", NULL, 1, 1, 0, JOF_BYTE) \
    /*
     * Push the current stack frame's `returnValue`. If no `JSOP_SETRVAL`
     * instruction has been executed in this stack frame, this is `undefined`.
     *
     * Every stack frame has a `returnValue` slot, used by top-level scripts,
     * generators, async functions, and derived class constructors. Plain
     * functions usually use `JSOP_RETURN` instead.
     *
     *   Category: Control flow
     *   Type: Return
     *   Operands:
     *   Stack: => rval
     */ \
    MACRO(JSOP_GETRVAL, "getrval", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Store `rval` in the current stack frame's `returnValue` slot.
     *
     *   Category: Control flow
     *   Type: Return
     *   Operands:
     *   Stack: rval =>
     */ \
    MACRO(JSOP_SETRVAL, "setrval", NULL, 1, 1, 0, JOF_BYTE) \
    /*
     * Stop execution and return the current stack frame's `returnValue`. If no
     * `JSOP_SETRVAL` instruction has been executed in this stack frame, this
     * is `undefined`.
     *
     * Also emitted at end of every script so consumers don't need to worry
     * about running off the end.
     *
     * If the current script is a derived class constructor, `returnValue` must
     * be an object. The script can use `JSOP_CHECKRETURN` to ensure this.
     *
     *   Category: Control flow
     *   Type: Return
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_RETRVAL, "retrval", NULL, 1, 0, 0, JOF_BYTE) \
    /*
     * Check the return value in a derived class constructor.
     *
     * -   If the current stack frame's `returnValue` is an object, do nothing.
     *
     * -   Otherwise, if the `returnValue` is undefined and `thisval` is an
     *     object, store `thisval` in the `returnValue` slot.
     *
     * -   Otherwise, throw a TypeError.
     *
     * This is exactly what has to happen when a derived class constructor
     * returns. `thisval` should be the current value of `this`, or
     * `MagicValue(JS_UNINITIALIZED_LEXICAL)` if `this` is uninitialized.
     *
     * Implements: [The [[Construct]] internal method of JS functions][1],
     * steps 13 and 15.
     *
     * [1]: https://tc39.es/ecma262/#sec-ecmascript-function-objects-construct-argumentslist-newtarget
     *
     *   Category: Control flow
     *   Type: Return
     *   Operands:
     *   Stack: thisval =>
     */ \
    MACRO(JSOP_CHECKRETURN, "checkreturn", NULL, 1, 1, 0, JOF_BYTE) \
    /*
     * Throw `exc`. (ノಠ益ಠ)ノ彡┴──┴
     *
     * This sets the pending exception to `exc` and jumps to error-handling
     * code. If we're in a `try` block, error handling adjusts the stack and
     * environment chain and resumes execution at the top of the `catch` or
     * `finally` block. Otherwise it starts unwinding the stack.
     *
     * Implements: [*ThrowStatement* Evaluation][1], step 3.
     *
     * This is also used in for-of loops. If the body of the loop throws an
     * exception, we catch it, close the iterator, then use `JSOP_THROW` to
     * rethrow.
     *
     * [1]: https://tc39.es/ecma262/#sec-throw-statement-runtime-semantics-evaluation
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands:
     *   Stack: exc =>
     */ \
    MACRO(JSOP_THROW, js_throw_str, NULL, 1, 1, 0, JOF_BYTE) \
    /*
     * Create and throw an Error object.
     *
     * Sometimes we know at emit time that an operation always throws. For
     * example, `delete super.prop;` is allowed in methods, but always throws a
     * ReferenceError.
     *
     * `msgNumber` must be one of the error codes listed in js/src/js.msg; it
     * determines the `.message` and [[Prototype]] of the new Error object. The
     * number of arguments in the error message must be 0.
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands: uint16_t msgNumber
     *   Stack: =>
     */ \
    MACRO(JSOP_THROWMSG, "throwmsg", NULL, 3, 0, 0, JOF_UINT16) \
    /*
     * Throw a TypeError for invalid assignment to a `const`. The environment
     * coordinate is used to get the variable name for the error message.
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands: uint8_t hops, uint24_t slot
     *   Stack: v => v
     */ \
    MACRO(JSOP_THROWSETALIASEDCONST, "throwsetaliasedconst", NULL, 5, 1, 1, JOF_ENVCOORD|JOF_NAME|JOF_DETECTING) \
    /*
     * Throw a TypeError for invalid assignment to the callee binding in a named
     * lambda, which is always a `const` binding. This is a different bytecode
     * than `JSOP_THROWSETCONST` because the named lambda callee, if not closed
     * over, does not have a frame slot to look up the name with for the error
     * message.
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands:
     *   Stack: v => v
     */ \
    MACRO(JSOP_THROWSETCALLEE, "throwsetcallee", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Throws a runtime TypeError for invalid assignment to an optimized
     * `const` binding. `localno` is used to get the variable name for the
     * error message.
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands: uint24_t localno
     *   Stack: v => v
     */ \
    MACRO(JSOP_THROWSETCONST, "throwsetconst", NULL, 4, 1, 1, JOF_LOCAL|JOF_NAME|JOF_DETECTING) \
    /*
     * No-op instruction that marks the top of the bytecode for a
     * *TryStatement*.
     *
     * The `jumpAtEndOffset` operand is the offset (relative to the current op)
     * of the `JSOP_GOTO` at the end of the try-block body. This is used by
     * bytecode analysis and JIT compilation.
     *
     * Location information for catch/finally blocks is stored in a side table,
     * `script->trynotes()`.
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands: int32_t jumpAtEndOffset
     *   Stack: =>
     */ \
    MACRO(JSOP_TRY, "try", NULL, 5, 0, 0, JOF_CODE_OFFSET) \
    /*
     * No-op instruction used by the exception unwinder to determine the
     * correct environment to unwind to when performing IteratorClose due to
     * destructuring.
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_TRY_DESTRUCTURING, "try-destructuring", NULL, 1, 0, 0, JOF_BYTE) \
    /*
     * Push and clear the pending exception. ┬──┬◡ﾉ(° -°ﾉ)
     *
     * This must be used only in the fixed sequence of instructions following a
     * `JSTRY_CATCH` span (see "Bytecode Invariants" above), as that's the only
     * way instructions would run with an exception pending.
     *
     * Used to implement catch-blocks, including the implicit ones generated as
     * part of for-of iteration.
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands:
     *   Stack: => exception
     */ \
    MACRO(JSOP_EXCEPTION, "exception", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Push `resumeIndex`.
     *
     * This value must be used only by `JSOP_GOSUB`, `JSOP_FINALLY`, and `JSOP_RETSUB`.
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands: uint24_t resumeIndex
     *   Stack: => resumeIndex
     */ \
    MACRO(JSOP_RESUMEINDEX, "resume-index", NULL, 4, 0, 1, JOF_RESUMEINDEX) \
    /*
     * Jump to the start of a `finally` block.
     *
     * `JSOP_GOSUB` is unusual: if the finally block finishes normally, it will
     * reach the `JSOP_RETSUB` instruction at the end, and control then
     * "returns" to the `JSOP_GOSUB` and picks up at the next instruction, like
     * a function call but within a single script and stack frame. (It's named
     * after the thing in BASIC.)
     *
     * We need this because a `try` block can terminate in several different
     * ways: control can flow off the end, return, throw an exception, `break`
     * with or without a label, or `continue`. Exceptions are handled
     * separately; but all those success paths are written as bytecode, and
     * each one needs to run the `finally` block before continuing with
     * whatever they were doing. They use `JSOP_GOSUB` for this. It is thus
     * normal for multiple `GOSUB` instructions in a script to target the same
     * `finally` block.
     *
     * Rules: `forwardOffset` must be positive and must target a
     * `JSOP_JUMPTARGET` instruction followed by `JSOP_FINALLY`. The
     * instruction immediately following `JSOP_GOSUB` in the script must be a
     * `JSOP_JUMPTARGET` instruction, and `resumeIndex` must be the index into
     * `script->resumeOffsets()` that points to that instruction.
     *
     * Note: This op doesn't actually push or pop any values. Its use count of
     * 2 is a lie to make the stack depth math work for this very odd control
     * flow instruction.
     *
     * `JSOP_GOSUB` is considered to have two "successors": the target of
     * `offset`, which is the actual next instruction to run; and the
     * instruction immediately following `JSOP_GOSUB`, even though it won't run
     * until later. We define the successor graph this way in order to support
     * knowing the stack depth at that instruction without first reading the
     * whole `finally` block.
     *
     * The stack depth at that instruction is, as it happens, the current stack
     * depth minus 2. So this instruction gets nuses == 2.
     *
     * Unfortunately there is a price to be paid in horribleness. When
     * `JSOP_GOSUB` runs, it leaves two values on the stack that the stack
     * depth math doesn't know about. It jumps to the finally block, where
     * `JSOP_FINALLY` again does nothing to the stack, but with a bogus def
     * count of 2, restoring balance to the accounting. If `JSOP_RETSUB` is
     * reached, it pops the two values (for real this time) and control
     * resumes at the instruction that follows JSOP_GOSUB in memory.
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands: int32_t forwardOffset
     *   Stack: false, resumeIndex =>
     */ \
    MACRO(JSOP_GOSUB, "gosub", NULL, 5, 2, 0, JOF_JUMP) \
    /*
     * No-op instruction that marks the start of a `finally` block. This has a
     * def count of 2, but the values are already on the stack (they're
     * actually left on the stack by `JSOP_GOSUB`).
     *
     * These two values must not be used except by `JSOP_RETSUB`.
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands:
     *   Stack: => false, resumeIndex
     */ \
    MACRO(JSOP_FINALLY, "finally", NULL, 1, 0, 2, JOF_BYTE) \
    /*
     * Jump back to the next instruction, or rethrow an exception, at the end
     * of a `finally` block. See `JSOP_GOSUB` for the explanation.
     *
     * If `throwing` is true, throw `v`. Otherwise, `v` must be a resume index;
     * jump to the corresponding offset within the script.
     *
     * The two values popped must be the ones notionally pushed by
     * `JSOP_FINALLY`.
     *
     *   Category: Control flow
     *   Type: Exceptions
     *   Operands:
     *   Stack: throwing, v =>
     */ \
    MACRO(JSOP_RETSUB, "retsub", NULL, 1, 2, 0, JOF_BYTE) \
    /*
     * Push `MagicValue(JS_UNINITIALIZED_LEXICAL)`, a magic value used to mark
     * a binding as uninitialized.
     *
     * This magic value must be used only by `JSOP_INIT*LEXICAL`.
     *
     *   Category: Variables and scopes
     *   Type: Initialization
     *   Operands:
     *   Stack: => uninitialized
     */ \
    MACRO(JSOP_UNINITIALIZED, "uninitialized", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Initialize an optimized local lexical binding; or mark it as
     * uninitialized.
     *
     * This stores the value `v` in the fixed slot `localno` in the current
     * stack frame. If `v` is the magic value produced by `JSOP_UNINITIALIZED`,
     * this marks the binding as uninitialized. Otherwise this initializes the
     * binding with value `v`.
     *
     * Implements: [CreateMutableBinding][1] step 3, substep "record that it is
     * uninitialized", and [InitializeBinding][2], for optimized locals. (Note:
     * this is how `const` bindings are initialized.)
     *
     * [1]: https://tc39.es/ecma262/#sec-declarative-environment-records-createmutablebinding-n-d
     * [2]: https://tc39.es/ecma262/#sec-declarative-environment-records-initializebinding-n-v
     *
     *   Category: Variables and scopes
     *   Type: Initialization
     *   Operands: uint24_t localno
     *   Stack: v => v
     */ \
    MACRO(JSOP_INITLEXICAL, "initlexical", NULL, 4, 1, 1, JOF_LOCAL|JOF_NAME|JOF_DETECTING) \
    /*
     * Initialize a global lexical binding; or mark it as uninitialized.
     *
     * Like `JSOP_INITLEXICAL` but for global lexicals.
     *
     *   Category: Variables and scopes
     *   Type: Initialization
     *   Operands: uint32_t nameIndex
     *   Stack: val => val
     */ \
    MACRO(JSOP_INITGLEXICAL, "initglexical", NULL, 5, 1, 1, JOF_ATOM|JOF_NAME|JOF_PROPINIT|JOF_GNAME|JOF_IC) \
    /*
     * Initialize an aliased lexical binding; or mark it as uninitialized.
     *
     * Like `JSOP_INITLEXICAL` but for aliased bindings.
     *
     * Note: There is no even-less-optimized `INITNAME` instruction because JS
     * doesn't need it. We always know statically which binding we're
     * initializing.
     *
     * `hops` is usually 0, but in `function f(a=eval("var b;")) { }`, the
     * argument `a` is initialized from inside a nested scope, so `hops == 1`.
     *
     *   Category: Variables and scopes
     *   Type: Initialization
     *   Operands: uint8_t hops, uint24_t slot
     *   Stack: v => v
     */ \
    MACRO(JSOP_INITALIASEDLEXICAL, "initaliasedlexical", NULL, 5, 1, 1, JOF_ENVCOORD|JOF_NAME|JOF_PROPINIT|JOF_DETECTING) \
    /*
     * Throw a ReferenceError if the optimized local `localno` is
     * uninitialized.
     *
     * `localno` must be the number of a fixed slot in the current stack frame
     * previously initialized or marked uninitialized using `JSOP_INITLEXICAL`.
     *
     * Typically used before `JSOP_GETLOCAL` or `JSOP_SETLOCAL`.
     *
     * Implements: [GetBindingValue][1] step 3 and [SetMutableBinding][2] step
     * 4 for declarative Environment Records.
     *
     * [1]: https://tc39.es/ecma262/#sec-declarative-environment-records-getbindingvalue-n-s
     * [2]: https://tc39.es/ecma262/#sec-declarative-environment-records-setmutablebinding-n-v-s
     *
     *   Category: Variables and scopes
     *   Type: Initialization
     *   Operands: uint24_t localno
     *   Stack: =>
     */ \
    MACRO(JSOP_CHECKLEXICAL, "checklexical", NULL, 4, 0, 0, JOF_LOCAL|JOF_NAME) \
    /*
     * Like `JSOP_CHECKLEXICAL` but for aliased bindings.
     *
     * Note: There are no `CHECKNAME` or `CHECKGNAME` instructions because
     * they're unnecessary. `JSOP_{GET,SET}{NAME,GNAME}` all check for
     * uninitialized lexicals and throw if needed.
     *
     *   Category: Variables and scopes
     *   Type: Initialization
     *   Operands: uint8_t hops, uint24_t slot
     *   Stack: =>
     */ \
    MACRO(JSOP_CHECKALIASEDLEXICAL, "checkaliasedlexical", NULL, 5, 0, 0, JOF_ENVCOORD|JOF_NAME) \
    /*
     * Throw a ReferenceError if the value on top of the stack is
     * `MagicValue(JS_UNINITIALIZED_LEXICAL)`. Used in derived class
     * constructors to check `this` (which needs to be initialized before use,
     * by calling `super()`).
     *
     * Implements: [GetThisBinding][1] step 3.
     *
     * [1]: https://tc39.es/ecma262/#sec-function-environment-records-getthisbinding
     *
     *   Category: Variables and scopes
     *   Type: Initialization
     *   Operands:
     *   Stack: this => this
     */ \
    MACRO(JSOP_CHECKTHIS, "checkthis", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Push the global environment onto the stack, unless the script has a
     * non-syntactic global scope. In that case, this acts like JSOP_BINDNAME.
     *
     * `nameIndex` is only used when acting like JSOP_BINDNAME.
     *
     *   Category: Variables and scopes
     *   Type: Looking up bindings
     *   Operands: uint32_t nameIndex
     *   Stack: => global
     */ \
    MACRO(JSOP_BINDGNAME, "bindgname", NULL, 5, 0, 1, JOF_ATOM|JOF_NAME|JOF_GNAME|JOF_IC) \
    /*
     * Look up a name on the environment chain and push the environment which
     * contains a binding for that name. If no such binding exists, push the
     * global lexical environment.
     *
     *   Category: Variables and scopes
     *   Type: Looking up bindings
     *   Operands: uint32_t nameIndex
     *   Stack: => env
     */ \
    MACRO(JSOP_BINDNAME, "bindname", NULL, 5, 0, 1, JOF_ATOM|JOF_NAME|JOF_IC) \
    /*
     * Find a binding on the environment chain and push its value.
     *
     * If the binding is an uninitialized lexical, throw a ReferenceError. If
     * no such binding exists, throw a ReferenceError unless the next
     * instruction is `JSOP_TYPEOF`, in which case push `undefined`.
     *
     * Implements: [ResolveBinding][1] followed by [GetValue][2]
     * (adjusted hackily for `typeof`).
     *
     * This is the fallback `GET` instruction that handles all unoptimized
     * cases. Optimized instructions follow.
     *
     * [1]: https://tc39.es/ecma262/#sec-resolvebinding
     * [2]: https://tc39.es/ecma262/#sec-getvalue
     *
     *   Category: Variables and scopes
     *   Type: Getting binding values
     *   Operands: uint32_t nameIndex
     *   Stack: => val
     */ \
    MACRO(JSOP_GETNAME, "getname", NULL, 5, 0, 1, JOF_ATOM|JOF_NAME|JOF_TYPESET|JOF_IC) \
    /*
     * Find a global binding and push its value.
     *
     * This searches the global lexical environment and, failing that, the
     * global object. (Unlike most declarative environments, the global lexical
     * environment can gain more bindings after compilation, possibly shadowing
     * global object properties.)
     *
     * This is an optimized version of `JSOP_GETNAME` that skips all local
     * scopes, for use when the name doesn't refer to any local binding.
     * `NonSyntacticVariablesObject`s break this optimization, so if the
     * current script has a non-syntactic global scope, this acts like
     * `JSOP_GETNAME`.
     *
     * Like `JSOP_GETNAME`, this throws a ReferenceError if no such binding is
     * found (unless the next instruction is `JSOP_TYPEOF`) or if the binding
     * is an uninitialized lexical.
     *
     *   Category: Variables and scopes
     *   Type: Getting binding values
     *   Operands: uint32_t nameIndex
     *   Stack: => val
     */ \
    MACRO(JSOP_GETGNAME, "getgname", NULL, 5, 0, 1, JOF_ATOM|JOF_NAME|JOF_TYPESET|JOF_GNAME|JOF_IC) \
    /*
     * Push the value of an argument that is stored in the stack frame
     * or in an `ArgumentsObject`.
     *
     *   Category: Variables and scopes
     *   Type: Getting binding values
     *   Operands: uint16_t argno
     *   Stack: => arguments[argno]
     */ \
    MACRO(JSOP_GETARG, "getarg", NULL, 3, 0, 1, JOF_QARG|JOF_NAME) \
    /*
     * Push the value of an optimized local variable.
     *
     * If the variable is an uninitialized lexical, push
     * `MagicValue(JS_UNINIITALIZED_LEXICAL)`.
     *
     *   Category: Variables and scopes
     *   Type: Getting binding values
     *   Operands: uint24_t localno
     *   Stack: => val
     */ \
    MACRO(JSOP_GETLOCAL, "getlocal", NULL, 4, 0, 1, JOF_LOCAL|JOF_NAME) \
    /*
     * Push the value of an aliased binding.
     *
     * Local bindings that aren't closed over or dynamically accessed are
     * stored in stack slots. Global and `with` bindings are object properties.
     * All other bindings are called "aliased" and stored in
     * `EnvironmentObject`s.
     *
     * Where possible, `ALIASED` instructions are used to access aliased
     * bindings.  (There's no difference in meaning between `ALIASEDVAR` and
     * `ALIASEDLEXICAL`.) Each of these instructions has operands `hops` and
     * `slot` that encode an [`EnvironmentCoordinate`][1], directions to the
     * binding from the current environment object.
     *
     * `hops` and `slot` must be valid for the current scope.
     *
     * `ALIASED` instructions can't be used when there's a dynamic scope (due
     * to non-strict `eval` or `with`) that might shadow the aliased binding.
     *
     * [1]: https://searchfox.org/mozilla-central/search?q=symbol:T_js%3A%3AEnvironmentCoordinate
     *
     *   Category: Variables and scopes
     *   Type: Getting binding values
     *   Operands: uint8_t hops, uint24_t slot
     *   Stack: => aliasedVar
     */ \
    MACRO(JSOP_GETALIASEDVAR, "getaliasedvar", NULL, 5, 0, 1, JOF_ENVCOORD|JOF_NAME|JOF_TYPESET|JOF_IC) \
    /*
     * Get the value of a module import by name and pushes it onto the stack.
     *
     *   Category: Variables and scopes
     *   Type: Getting binding values
     *   Operands: uint32_t nameIndex
     *   Stack: => val
     */ \
    MACRO(JSOP_GETIMPORT, "getimport", NULL, 5, 0, 1, JOF_ATOM|JOF_NAME|JOF_TYPESET|JOF_IC) \
    /*
     * Get the value of a binding from the environment `env`. If the name is
     * not bound in `env`, throw a ReferenceError.
     *
     * `env` must be an environment currently on the environment chain, pushed
     * by `JSOP_BINDNAME`.
     *
     * Note: `JSOP_BINDNAME` and `JSOP_GETBOUNDNAME` are the two halves of the
     * `JSOP_GETNAME` operation: finding and reading a variable. This
     * decomposed version is needed to implement the compound assignment and
     * increment/decrement operators, which get and then set a variable. The
     * spec says the variable lookup is done only once. If we did the lookup
     * twice, there would be observable bugs, thanks to dynamic scoping. We
     * could set the wrong variable or call proxy traps incorrectly.
     *
     * Implements: [GetValue][1] steps 4 and 6.
     *
     * [1]: https://tc39.es/ecma262/#sec-getvalue
     *
     *   Category: Variables and scopes
     *   Type: Getting binding values
     *   Operands: uint32_t nameIndex
     *   Stack: env => v
     */ \
    MACRO(JSOP_GETBOUNDNAME, "getboundname", NULL, 5, 1, 1, JOF_ATOM|JOF_NAME|JOF_TYPESET|JOF_IC) \
    /*
     * Push the value of an intrinsic onto the stack.
     *
     * Non-standard. Intrinsics are slots in the intrinsics holder object (see
     * `GlobalObject::getIntrinsicsHolder`), which is used in lieu of global
     * bindings in self-hosting code.
     *
     *   Category: Variables and scopes
     *   Type: Getting binding values
     *   Operands: uint32_t nameIndex
     *   Stack: => intrinsic[name]
     */ \
    MACRO(JSOP_GETINTRINSIC, "getintrinsic", NULL, 5, 0, 1, JOF_ATOM|JOF_NAME|JOF_TYPESET|JOF_IC) \
    /*
     * Pushes the currently executing function onto the stack.
     *
     * The current script must be a function script.
     *
     * Used to implement `super`. This is also used sometimes as a minor
     * optimization when a named function expression refers to itself by name:
     *
     *     f = function fac(n) {  ... fac(n - 1) ... };
     *
     * This lets us optimize away a lexical environment that contains only the
     * binding for `fac`, unless it's otherwise observable (via `with`, `eval`,
     * or a nested closure).
     *
     *   Category: Variables and scopes
     *   Type: Getting binding values
     *   Operands:
     *   Stack: => callee
     */ \
    MACRO(JSOP_CALLEE, "callee", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Load the callee stored in a CallObject on the environment chain. The
     * numHops operand is the number of environment objects to skip on the
     * environment chain.
     *
     *   Category: Variables and scopes
     *   Type: Getting binding values
     *   Operands: uint8_t numHops
     *   Stack: => callee
     */ \
    MACRO(JSOP_ENVCALLEE, "envcallee", NULL, 2, 0, 1, JOF_UINT8) \
    /*
     * Assign `val` to the binding in `env` with the name given by `nameIndex`.
     * Throw a ReferenceError if the binding is an uninitialized lexical.
     * This can call setters and/or proxy traps.
     *
     * `env` must be an environment currently on the environment chain,
     * pushed by `JSOP_BINDNAME`.
     *
     * This is the fallback `SET` instruction that handles all unoptimized
     * cases. Optimized instructions follow.
     *
     * Implements: [PutValue][1] steps 5 and 7 for unoptimized bindings.
     *
     * Note: `JSOP_BINDNAME` and `JSOP_SETNAME` are the two halves of simple
     * assignment: finding and setting a variable. They are two separate
     * instructions because, per spec, the "finding" part happens before
     * evaluating the right-hand side of the assignment, and the "setting" part
     * after. Optimized cases don't need a `BIND` instruction because the
     * "finding" is done statically.
     *
     * [1]: https://tc39.es/ecma262/#sec-putvalue
     *
     *   Category: Variables and scopes
     *   Type: Setting binding values
     *   Operands: uint32_t nameIndex
     *   Stack: env, val => val
     */ \
    MACRO(JSOP_SETNAME, "setname", NULL, 5, 2, 1, JOF_ATOM|JOF_NAME|JOF_PROPSET|JOF_DETECTING|JOF_CHECKSLOPPY|JOF_IC) \
    /*
     * Like `JSOP_SETNAME`, but throw a TypeError if there is no binding for
     * the specified name in `env`, or if the binding is immutable (a `const`
     * or read-only property).
     *
     * Implements: [PutValue][1] steps 5 and 7 for strict mode code.
     *
     * [1]: https://tc39.es/ecma262/#sec-putvalue
     *
     *   Category: Variables and scopes
     *   Type: Setting binding values
     *   Operands: uint32_t nameIndex
     *   Stack: env, val => val
     */ \
    MACRO(JSOP_STRICTSETNAME, "strict-setname", NULL, 5, 2, 1, JOF_ATOM|JOF_NAME|JOF_PROPSET|JOF_DETECTING|JOF_CHECKSTRICT|JOF_IC) \
    /*
     * Like `JSOP_SETNAME`, but for assigning to globals. `env` must be an
     * environment pushed by `JSOP_BINDGNAME`.
     *
     *   Category: Variables and scopes
     *   Type: Setting binding values
     *   Operands: uint32_t nameIndex
     *   Stack: env, val => val
     */ \
    MACRO(JSOP_SETGNAME, "setgname", NULL, 5, 2, 1, JOF_ATOM|JOF_NAME|JOF_PROPSET|JOF_DETECTING|JOF_GNAME|JOF_CHECKSLOPPY|JOF_IC) \
    /*
     * Like `JSOP_STRICTSETGNAME`, but for assigning to globals. `env` must be
     * an environment pushed by `JSOP_BINDGNAME`.
     *
     *   Category: Variables and scopes
     *   Type: Setting binding values
     *   Operands: uint32_t nameIndex
     *   Stack: env, val => val
     */ \
    MACRO(JSOP_STRICTSETGNAME, "strict-setgname", NULL, 5, 2, 1, JOF_ATOM|JOF_NAME|JOF_PROPSET|JOF_DETECTING|JOF_GNAME|JOF_CHECKSTRICT|JOF_IC) \
    /*
     * Assign `val` to an argument binding that's stored in the stack frame or
     * in an `ArgumentsObject`.
     *
     *   Category: Variables and scopes
     *   Type: Setting binding values
     *   Operands: uint16_t argno
     *   Stack: val => val
     */ \
    MACRO(JSOP_SETARG, "setarg", NULL, 3, 1, 1, JOF_QARG|JOF_NAME) \
    /*
     * Assign to an optimized local binding.
     *
     *   Category: Variables and scopes
     *   Type: Setting binding values
     *   Operands: uint24_t localno
     *   Stack: v => v
     */ \
    MACRO(JSOP_SETLOCAL, "setlocal", NULL, 4, 1, 1, JOF_LOCAL|JOF_NAME|JOF_DETECTING) \
    /*
     * Assign to an aliased binding.
     *
     * Implements: [SetMutableBinding for declarative Environment Records][1],
     * in certain cases where it's known that the binding exists, is mutable,
     * and has been initialized.
     *
     * [1]: https://tc39.es/ecma262/#sec-declarative-environment-records-setmutablebinding-n-v-s
     *
     *   Category: Variables and scopes
     *   Type: Setting binding values
     *   Operands: uint8_t hops, uint24_t slot
     *   Stack: val => val
     */ \
    MACRO(JSOP_SETALIASEDVAR, "setaliasedvar", NULL, 5, 1, 1, JOF_ENVCOORD|JOF_NAME|JOF_PROPSET|JOF_DETECTING) \
    /*
     * Assign to an intrinsic.
     *
     * Nonstandard. Intrinsics are used in lieu of global bindings in self-
     * hosted code. The value is actually stored in the intrinsics holder
     * object, `GlobalObject::getIntrinsicsHolder`. (Self-hosted code doesn't
     * have many global `var`s, but it has many `function`s.)
     *
     *   Category: Variables and scopes
     *   Type: Setting binding values
     *   Operands: uint32_t nameIndex
     *   Stack: val => val
     */ \
    MACRO(JSOP_SETINTRINSIC, "setintrinsic", NULL, 5, 1, 1, JOF_ATOM|JOF_NAME|JOF_DETECTING) \
    /*
     * Push a lexical environment onto the environment chain.
     *
     * The `LexicalScope` indicated by `lexicalScopeIndex` determines the shape
     * of the new `LexicalEnvironmentObject`. All bindings in the new
     * environment are marked as uninitialized.
     *
     * Implements: [Evaluation of *Block*][1], steps 1-4.
     *
     * #### Fine print for environment chain instructions
     *
     * The following rules for `JSOP_{PUSH,POP}LEXICALENV` also apply to
     * `JSOP_{PUSH,POP}VARENV` and `JSOP_{ENTER,LEAVE}WITH`.
     *
     * Each `JSOP_POPLEXICALENV` instruction matches a particular
     * `JSOP_PUSHLEXICALENV` instruction in the same script and must have the
     * same scope and stack depth as the instruction immediately after that
     * `PUSHLEXICALENV`.
     *
     * `JSOP_PUSHLEXICALENV` enters a scope that extends to some set of
     * instructions in the script. Code must not jump into or out of this
     * region: control can enter only by executing `PUSHLEXICALENV` and can
     * exit only by executing a `POPLEXICALENV` or by exception unwinding. (A
     * `JSOP_POPLEXICALENV` is always emitted at the end of the block, and
     * extra copies are emitted on "exit slides", where a `break`, `continue`,
     * or `return` statement exits the scope.)
     *
     * The script's `JSScript::scopeNotes()` must identify exactly which
     * instructions begin executing in this scope. Typically this means a
     * single entry marking the contiguous chunk of bytecode from the
     * instruction after `JSOP_PUSHLEXICALENV` to `JSOP_POPLEXICALENV`
     * (inclusive); but if that range contains any instructions on exit slides,
     * after a `JSOP_POPLEXICALENV`, then those must be correctly noted as
     * *outside* the scope.
     *
     * [1]: https://tc39.es/ecma262/#sec-block-runtime-semantics-evaluation
     *
     *   Category: Variables and scopes
     *   Type: Entering and leaving environments
     *   Operands: uint32_t lexicalScopeIndex
     *   Stack: =>
     */ \
    MACRO(JSOP_PUSHLEXICALENV, "pushlexicalenv", NULL, 5, 0, 0, JOF_SCOPE) \
    /*
     * Pop a lexical environment from the environment chain.
     *
     * See `JSOP_PUSHLEXICALENV` for the fine print.
     *
     *   Category: Variables and scopes
     *   Type: Entering and leaving environments
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_POPLEXICALENV, "poplexicalenv", NULL, 1, 0, 0, JOF_BYTE) \
    /*
     * No-op instruction that indicates leaving an optimized lexical scope.
     *
     * If all bindings in a lexical scope are optimized into stack slots, then
     * the runtime environment objects for that scope are optimized away. No
     * `JSOP_{PUSH,POP}LEXICALENV` instructions are emitted. However, the
     * debugger still needs to be notified when control exits a scope; that's
     * what this instruction does.
     *
     * The last instruction in a lexical scope, as indicated by scope notes,
     * must be marked with either this instruction (if the scope is optimized)
     * or `JSOP_POPLEXICALENV` (if not).
     *
     *   Category: Variables and scopes
     *   Type: Entering and leaving environments
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_DEBUGLEAVELEXICALENV, "debugleavelexicalenv", NULL, 1, 0, 0, JOF_BYTE) \
    /*
     * Recreate the current block on the environment chain with a fresh block
     * with uninitialized bindings. This implements the behavior of inducing a
     * fresh lexical environment for every iteration of a for-in/of loop whose
     * loop-head has a (captured) lexical declaration.
     *
     *   Category: Variables and scopes
     *   Type: Entering and leaving environments
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_RECREATELEXICALENV, "recreatelexicalenv", NULL, 1, 0, 0, JOF_BYTE) \
    /*
     * Replace the current block on the environment chain with a fresh block
     * that copies all the bindings in the block. This implements the behavior
     * of inducing a fresh lexical environment for every iteration of a
     * `for(let ...; ...; ...)` loop, if any declarations induced by such a
     * loop are captured within the loop.
     *
     *   Category: Variables and scopes
     *   Type: Entering and leaving environments
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_FRESHENLEXICALENV, "freshenlexicalenv", NULL, 1, 0, 0, JOF_BYTE) \
    /*
     * Push a var environment onto the environment chain.
     *
     * Like `JSOP_PUSHLEXICALENV`, but pushes a `VarEnvironmentObject` rather
     * than a `LexicalEnvironmentObject`. The difference is that non-strict
     * direct `eval` can add bindings to a var environment; see `VarScope` in
     * Scope.h.
     *
     * See `JSOP_PUSHLEXICALENV` for the fine print.
     *
     * Implements: Places in the spec where the VariableEnvironment is set:
     *
     * -   The bit in [PerformEval][1] where, in strict direct eval, the new
     *     eval scope is taken as *varEnv* and becomes "*runningContext*'s
     *     VariableEnvironment".
     *
     * -   The weird scoping rules for functions with default parameter
     *     expressions, as specified in [FunctionDeclarationInstantiation][2]
     *     step 28 ("NOTE: A separate Environment Record is needed...") and
     *     [IteratorBindingInitialization for *FormalParameter* and
     *     *FormalRestParameter*][3].
     *
     * Note: The spec also pushes a new VariableEnvironment on entry to every
     * function, but the VM takes care of that as part of pushing the stack
     * frame, before the function script starts to run, so `JSOP_PUSHVARENV` is
     * not needed.
     *
     * [1]: https://tc39.es/ecma262/#sec-performeval
     * [2]: https://tc39.es/ecma262/#sec-functiondeclarationinstantiation
     * [3]: https://tc39.es/ecma262/#sec-function-definitions-runtime-semantics-iteratorbindinginitialization
     *
     *   Category: Variables and scopes
     *   Type: Entering and leaving environments
     *   Operands: uint32_t scopeIndex
     *   Stack: =>
     */ \
    MACRO(JSOP_PUSHVARENV, "pushvarenv", NULL, 5, 0, 0, JOF_SCOPE) \
    /*
     * Pop a `VarEnvironmentObject` from the environment chain.
     *
     * See `JSOP_PUSHLEXICALENV` for the fine print.
     *
     *   Category: Variables and scopes
     *   Type: Entering and leaving environments
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_POPVARENV, "popvarenv", NULL, 1, 0, 0, JOF_BYTE) \
    /*
     * Push a `WithEnvironmentObject` wrapping ToObject(`val`) to the
     * environment chain.
     *
     * Implements: [Evaluation of `with` statements][1], steps 2-6.
     *
     * Operations that may need to consult a WithEnvironment can't be correctly
     * implemented using optimized instructions like `JSOP_GETLOCAL`. A script
     * must use the deoptimized `JSOP_GETNAME`, `BINDNAME`, `SETNAME`, and
     * `DELNAME` instead. Since those instructions don't work correctly with
     * optimized locals and arguments, all bindings in scopes enclosing a
     * `with` statement are marked as "aliased" and deoptimized too.
     *
     * See `JSOP_PUSHLEXICALENV` for the fine print.
     *
     * [1]: https://tc39.es/ecma262/#sec-with-statement-runtime-semantics-evaluation
     *
     *   Category: Variables and scopes
     *   Type: Entering and leaving environments
     *   Operands: uint32_t staticWithIndex
     *   Stack: val =>
     */ \
    MACRO(JSOP_ENTERWITH, "enterwith", NULL, 5, 1, 0, JOF_SCOPE) \
    /*
     * Pop a `WithEnvironmentObject` from the environment chain.
     *
     * See `JSOP_PUSHLEXICALENV` for the fine print.
     *
     * Implements: [Evaluation of `with` statements][1], step 8.
     *
     * [1]: https://tc39.es/ecma262/#sec-with-statement-runtime-semantics-evaluation
     *
     *   Category: Variables and scopes
     *   Type: Entering and leaving environments
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_LEAVEWITH, "leavewith", NULL, 1, 0, 0, JOF_BYTE) \
    /*
     * Push the current VariableEnvironment (the environment on the environment
     * chain designated to receive new variables).
     *
     * Implements: [Annex B.3.3.1, changes to FunctionDeclarationInstantiation
     * for block-level functions][1], step 1.a.ii.3.a, and similar steps in
     * other Annex B.3.3 algorithms, when setting the function's second binding
     * can't be optimized.
     *
     * [1]: https://tc39.es/ecma262/#sec-web-compat-functiondeclarationinstantiation
     *
     *   Category: Variables and scopes
     *   Type: Creating and deleting bindings
     *   Operands:
     *   Stack: => env
     */ \
    MACRO(JSOP_BINDVAR, "bindvar", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Create a new binding on the current VariableEnvironment (the environment
     * on the environment chain designated to receive new variables).
     *
     * `JSOP_DEF{VAR,LET,CONST,FUN}` instructions must appear in the script
     * before anything else that might add bindings to the environment, and
     * only once per binding. There must be a correct entry for the new binding
     * in `script->bodyScope()`. (All this ensures that at run time, there is
     * no existing conflicting binding. We check before running the script, in
     * `js::CheckGlobalOrEvalDeclarationConflicts`.)
     *
     * Throw a SyntaxError if the current VariableEnvironment is the global
     * environment and a binding with the same name exists on the global
     * lexical environment.
     *
     * This is used for global scripts and also in some cases for function
     * scripts where use of dynamic scoping inhibits optimization.
     *
     *   Category: Variables and scopes
     *   Type: Creating and deleting bindings
     *   Operands: uint32_t nameIndex
     *   Stack: =>
     */ \
    MACRO(JSOP_DEFVAR, "defvar", NULL, 5, 0, 0, JOF_ATOM) \
    /*
     * Create a new binding for the given function on the current scope.
     *
     * `fun` must be a function object with an explicit name. The new
     * variable's name is `fun->explicitName()`, and its value is `fun`. In
     * global scope, this creates a new property on the global object.
     *
     * Implements: The body of the loop in [GlobalDeclarationInstantiation][1]
     * step 17 ("For each Parse Node *f* in *functionsToInitialize*...") and
     * the corresponding loop in [EvalDeclarationInstantiation][2].
     *
     * [1]: https://tc39.es/ecma262/#sec-globaldeclarationinstantiation
     * [2]: https://tc39.es/ecma262/#sec-evaldeclarationinstantiation
     *
     *   Category: Variables and scopes
     *   Type: Creating and deleting bindings
     *   Operands:
     *   Stack: fun =>
     */ \
    MACRO(JSOP_DEFFUN, "deffun", NULL, 1, 1, 0, JOF_BYTE) \
    /*
     * Create a new mutable binding in the global lexical environment. Throw a
     * SyntaxError if a binding with the same name already exists on that
     * environment, or if a var binding with the same name exists on the
     * global.
     *
     *   Category: Variables and scopes
     *   Type: Creating and deleting bindings
     *   Operands: uint32_t nameIndex
     *   Stack: =>
     */ \
    MACRO(JSOP_DEFLET, "deflet", NULL, 5, 0, 0, JOF_ATOM) \
    /*
     * Create a new constant binding in the global lexical environment.
     *
     * Throw a SyntaxError if a binding with the same name already exists in
     * that environment, or if a var binding with the same name exists on the
     * global.
     *
     *   Category: Variables and scopes
     *   Type: Creating and deleting bindings
     *   Operands: uint32_t nameIndex
     *   Stack: =>
     */ \
    MACRO(JSOP_DEFCONST, "defconst", NULL, 5, 0, 0, JOF_ATOM) \
    /*
     * Look up a variable on the environment chain and delete it. Push `true`
     * on success (if a binding was deleted, or if no such binding existed in
     * the first place), `false` otherwise (most kinds of bindings can't be
     * deleted).
     *
     * Implements: [`delete` *Identifier*][1], which [is a SyntaxError][2] in
     * strict mode code.
     *
     *    [1]: https://tc39.es/ecma262/#sec-delete-operator-runtime-semantics-evaluation
     *    [2]: https://tc39.es/ecma262/#sec-delete-operator-static-semantics-early-errors
     *
     *   Category: Variables and scopes
     *   Type: Creating and deleting bindings
     *   Operands: uint32_t nameIndex
     *   Stack: => succeeded
     */ \
    MACRO(JSOP_DELNAME, "delname", NULL, 5, 0, 1, JOF_ATOM|JOF_NAME|JOF_CHECKSLOPPY) \
    /*
     * Create and push the `arguments` object for the current function activation.
     *
     * When it exists, `arguments` is stored in an ordinary local variable.
     * `JSOP_ARGUMENTS` is used in function preludes, to populate that variable
     * before the function body runs, *not* each time `arguments` appears in a
     * function.
     *
     * If a function clearly doesn't use `arguments`, we optimize it away when
     * emitting bytecode. The function's script won't use `JSOP_ARGUMENTS` at
     * all.
     *
     * The current script must be a function script. This instruction must
     * execute at most once per function activation.
     *
     * #### Optimized arguments
     *
     * If `script->needsArgsObj()` is false, no ArgumentsObject is created.
     * Instead, `MagicValue(JS_OPTIMIZED_ARGUMENTS)` is pushed.
     *
     * This optimization imposes no restrictions on bytecode. Rather,
     * `js::jit::AnalyzeArgumentsUsage` examines the bytecode and enables the
     * optimization only if all uses of `arguments` are optimizable.  Each
     * execution engine must know what the analysis considers optimizable and
     * cope with the magic value when it is used in those ways.
     *
     * Example 1: `arguments[0]` is supported; therefore the interpreter's
     * implementation of `JSOP_GETELEM` checks for optimized arguments (see
     * `GetElemOptimizedArguments`).
     *
     * Example 2: `f.apply(this, arguments)` is supported; therefore our
     * implementation of `Function.prototype.apply` checks for optimized
     * arguments (`see js::fun_apply`), and all `JSOP_FUNAPPLY` implementations
     * must check for cases where `f.apply` turns out to be any other function
     * (see `GuardFunApplyArgumentsOptimization`).
     *
     * It's not documented anywhere exactly which opcodes support
     * `JS_OPTIMIZED_ARGUMENTS`; see the source of `AnalyzeArgumentsUsage`.
     *
     *   Category: Variables and scopes
     *   Type: Function environment setup
     *   Operands:
     *   Stack: => arguments
     */ \
    MACRO(JSOP_ARGUMENTS, "arguments", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Create and push the rest parameter array for current function call.
     *
     * This must appear only in function scripts.
     *
     *   Category: Variables and scopes
     *   Type: Function environment setup
     *   Operands:
     *   Stack: => rest
     */ \
    MACRO(JSOP_REST, "rest", NULL, 1, 0, 1, JOF_BYTE|JOF_TYPESET|JOF_IC) \
    /*
     * Determines the `this` value for current function frame and pushes it
     * onto the stack.
     *
     * In functions, `this` is stored in a local variable. This instruction is
     * used in the function prologue to get the value to initialize that
     * variable.  (This doesn't apply to arrow functions, becauses they don't
     * have a `this` binding; also, `this` is optimized away if it's unused.)
     *
     * Functions that have a `this` binding have a local variable named
     * `".this"`, which is initialized using this instruction in the function
     * prologue.
     *
     *   Category: Variables and scopes
     *   Type: Function environment setup
     *   Operands:
     *   Stack: => this
     */ \
    MACRO(JSOP_FUNCTIONTHIS, "functionthis", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Pop the top value from the stack and discard it.
     *
     *   Category: Stack operations
     *   Operands:
     *   Stack: v =>
     */ \
    MACRO(JSOP_POP, "pop", NULL, 1, 1, 0, JOF_BYTE) \
    /*
     * Pop the top `n` values from the stack. `n` must be <= the current stack
     * depth.
     *
     *   Category: Stack operations
     *   Operands: uint16_t n
     *   Stack: v[n-1], ..., v[1], v[0] =>
     *   nuses: n
     */ \
    MACRO(JSOP_POPN, "popn", NULL, 3, -1, 0, JOF_UINT16) \
    /*
     * Push a copy of the top value on the stack.
     *
     *   Category: Stack operations
     *   Operands:
     *   Stack: v => v, v
     */ \
    MACRO(JSOP_DUP, "dup", NULL, 1, 1, 2, JOF_BYTE) \
    /*
     * Duplicate the top two values on the stack.
     *
     *   Category: Stack operations
     *   Operands:
     *   Stack: v1, v2 => v1, v2, v1, v2
     */ \
    MACRO(JSOP_DUP2, "dup2", NULL, 1, 2, 4, JOF_BYTE) \
    /*
     * Push a copy of the nth value from the top of the stack.
     *
     * `n` must be less than the current stack depth.
     *
     *   Category: Stack operations
     *   Operands: uint24_t n
     *   Stack: v[n], v[n-1], ..., v[1], v[0] =>
     *          v[n], v[n-1], ..., v[1], v[0], v[n]
     */ \
    MACRO(JSOP_DUPAT, "dupat", NULL, 4, 0, 1, JOF_UINT24) \
    /*
     * Swap the top two values on the stack.
     *
     *   Category: Stack operations
     *   Operands:
     *   Stack: v1, v2 => v2, v1
     */ \
    MACRO(JSOP_SWAP, "swap", NULL, 1, 2, 2, JOF_BYTE) \
    /*
     * Pick the nth element from the stack and move it to the top of the stack.
     *
     *   Category: Stack operations
     *   Operands: uint8_t n
     *   Stack: v[n], v[n-1], ..., v[1], v[0] => v[n-1], ..., v[1], v[0], v[n]
     */ \
    MACRO(JSOP_PICK, "pick", NULL, 2, 0, 0, JOF_UINT8) \
    /*
     * Move the top of the stack value under the `n`th element of the stack.
     * `n` must not be 0.
     *
     *   Category: Stack operations
     *   Operands: uint8_t n
     *   Stack: v[n], v[n-1], ..., v[1], v[0] => v[0], v[n], v[n-1], ..., v[1]
     */ \
    MACRO(JSOP_UNPICK, "unpick", NULL, 2, 0, 0, JOF_UINT8) \
    /*
     * Do nothing. This is used when we need distinct bytecode locations for
     * various mechanisms.
     *
     *   Category: Other
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_NOP, "nop", NULL, 1, 0, 0, JOF_BYTE) \
    /*
     * No-op instruction used to speed up pc-to-line mapping.
     *
     *   Category: Other
     *   Operands: uint32_t lineno
     *   Stack: =>
     */ \
    MACRO(JSOP_LINENO, "lineno", NULL, 5, 0, 0, JOF_UINT32) \
    /*
     * No-op instruction used by the decompiler to produce nicer error messages
     * about destructuring code.
     *
     *   Category: Other
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_NOP_DESTRUCTURING, "nop-destructuring", NULL, 1, 0, 0, JOF_BYTE) \
    /*
     * No-op instruction only emitted in some self-hosted functions. Not
     * handled by the JITs or Baseline Interpreter so the script always runs in
     * the C++ interpreter.
     *
     *   Category: Other
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_FORCEINTERPRETER, "forceinterpreter", NULL, 1, 0, 0, JOF_BYTE) \
    /*
     * Examine the top stack value, asserting that it's either a self-hosted
     * function or a self-hosted intrinsic. This does nothing in a non-debug
     * build.
     *
     *   Category: Other
     *   Operands:
     *   Stack: checkVal => checkVal
     */ \
    MACRO(JSOP_DEBUGCHECKSELFHOSTED, "debug-checkselfhosted", NULL, 1, 1, 1, JOF_BYTE) \
    /*
     * Push a boolean indicating if instrumentation is active.
     *
     *   Category: Other
     *   Operands:
     *   Stack: => val
     */ \
    MACRO(JSOP_INSTRUMENTATION_ACTIVE, "instrumentationActive", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Push the instrumentation callback for the current realm.
     *
     *   Category: Other
     *   Operands:
     *   Stack: => val
     */ \
    MACRO(JSOP_INSTRUMENTATION_CALLBACK, "instrumentationCallback", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Push the current script's instrumentation ID.
     *
     *   Category: Other
     *   Operands:
     *   Stack: => val
     */ \
    MACRO(JSOP_INSTRUMENTATION_SCRIPT_ID, "instrumentationScriptId", NULL, 1, 0, 1, JOF_BYTE) \
    /*
     * Break in the debugger, if one is attached. Otherwise this is a no-op.
     *
     * The [`Debugger` API][1] offers a way to hook into this instruction.
     *
     * Implements: [Evaluation for *DebuggerStatement*][2].
     *
     * [1]: https://developer.mozilla.org/en-US/docs/Tools/Debugger-API/Debugger
     * [2]: https://tc39.es/ecma262/#sec-debugger-statement-runtime-semantics-evaluation
     *
     *   Category: Other
     *   Operands:
     *   Stack: =>
     */ \
    MACRO(JSOP_DEBUGGER, "debugger", NULL, 1, 0, 0, JOF_BYTE)

// clang-format on

/*
 * In certain circumstances it may be useful to "pad out" the opcode space to
 * a power of two.  Use this macro to do so.
 */
#define FOR_EACH_TRAILING_UNUSED_OPCODE(MACRO) \
  MACRO(240)                                   \
  MACRO(241)                                   \
  MACRO(242)                                   \
  MACRO(243)                                   \
  MACRO(244)                                   \
  MACRO(245)                                   \
  MACRO(246)                                   \
  MACRO(247)                                   \
  MACRO(248)                                   \
  MACRO(249)                                   \
  MACRO(250)                                   \
  MACRO(251)                                   \
  MACRO(252)                                   \
  MACRO(253)                                   \
  MACRO(254)                                   \
  MACRO(255)

namespace js {

// Sanity check that opcode values and trailing unused opcodes completely cover
// the [0, 256) range.  Avert your eyes!  You don't want to know how the
// sausage gets made.

// clang-format off
#define PLUS_ONE(...) \
    + 1
#define TRAILING_VALUE_AND_VALUE_PLUS_ONE(val) \
    val) && (val + 1 ==
static_assert((0 FOR_EACH_OPCODE(PLUS_ONE) ==
               FOR_EACH_TRAILING_UNUSED_OPCODE(TRAILING_VALUE_AND_VALUE_PLUS_ONE)
               256),
              "trailing unused opcode values monotonically increase "
              "from JSOP_LIMIT to 255");
#undef TRAILING_VALUE_AND_VALUE_PLUS_ONE
#undef PLUS_ONE
// clang-format on

// Define JSOP_*_LENGTH constants for all ops.
#define DEFINE_LENGTH_CONSTANT(op, name, image, len, ...) \
  constexpr size_t op##_LENGTH = len;
FOR_EACH_OPCODE(DEFINE_LENGTH_CONSTANT)
#undef DEFINE_LENGTH_CONSTANT

}  // namespace js

#endif  // vm_Opcodes_h
