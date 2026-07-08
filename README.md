# 🧶 Cotton Programming Language

<p align="center">
  <strong>The ultimate hybrid language weaving Python's readability, JS's async, Rust's safety, and C/C++'s raw performance into one.</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Language-Rust-orange.svg" alt="Implementation Language">
  <img src="https://img.shields.io/badge/Backend-Native-blue.svg" alt="Backend">
  <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License">
</p>

---

## 💡 Philosophy: Why Cotton?

Modern developers face a cruel choice. Choosing Python or JavaScript for high productivity means sacrificing performance and low-level memory control. On the other hand, choosing C/C++ or Rust means enduring complex syntax and the stress of a notorious borrow checker.

**Cotton** was born to softly and comfortably wrap the complex and sharp edges of modern system languages, just like cotton. To prevent developers from suffering from complex memory symbols or function coloring problems, the compiler handles the heavy lifting behind the scenes.

> *"Cotton is a programming language that blends seamlessly into daily life, like the most comfortable clothes."*

---

## 🚀 Key Highlights

### Dual-Engine Compilation (JIT + AOT Hybrid)
* **Development Mode (`cotton run`)**: A high-speed Just-In-Time (JIT) interpreter runs instantly as you modify code, providing quick feedback.
* **Production Mode (`cotton build`)**: A custom native Ahead-Of-Time (AOT) backend compiles the code into a highly optimized, single native binary without any garbage collection overhead.

### Auto-Borrow Memory Management
* Eliminates the need for a garbage collector (GC), preventing runtime overhead or "Stop-The-World" pauses.
* The compiler performs Region-based Lifecycle Analysis, automatically and safely calculating ownership and moves without requiring developers to write numerous reference symbols (`&`).

### Colorless Async
* Completely solves the notorious "Function Coloring Problem" found in JavaScript and Python.
* You can seamlessly make asynchronous calls inside regular functions without architectural constraints. Internally, a Go-style M:N hybrid green-thread scheduler handles the non-blocking execution.

### Zero-Glue Interoperability (FFI)
* No need to write complex binding or glue code to use libraries from other languages.
* The Cotton compiler natively parses and imports C header files or Python modules directly at the language level.

### Comptime Meta-programming & Inline DSL
* Supports a `comptime` environment where you can execute code at compile time using the exact same Cotton syntax, without learning a separate macro system.
* Recognizes SQL queries or web templates (JSX) as inline Domain-Specific Languages (DSLs) rather than raw strings, allowing the compiler to validate them perfectly at compile time.

---

## 🛠️ System Architecture

The Cotton compiler is written in Rust. The compilation pipeline guides the source code to a native executable without relying on heavy external compiler frameworks: