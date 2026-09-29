#pragma once

// Crash reporter: logs fault address + module + recent engine-contact notes
// to LuaAPI.crash.log when the process dies unhandled. Async-safe by design:
// static buffer, no heap, no locks, no CRT formatting in the handler. Chains
// to any previously installed filter.

namespace LuaAPI {
namespace CrashReporter {

// Install once at init (before hooks). Safe to call twice.
void Init();

// Re-assert our filter (something - spawner, ddraw, engine - replaces the
// UEF after init, which silenced the reporter). Cheap; call every MainLoop.
// Preserves the chain to whoever was installed.
void Reassert();

// Record an engine-contact point (call sites only, game thread). Keeps the
// last 32 tags; the handler dumps the tail. Tag must be a string literal
// (pointer stored, never copied).
void Note(const char* tag);

} // namespace CrashReporter
} // namespace LuaAPI
