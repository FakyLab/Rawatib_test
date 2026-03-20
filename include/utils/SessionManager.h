#pragma once

// ── SessionManager ────────────────────────────────────────────────────────
//
// Provides cryptographic session binding for the admin unlock state.
//
// Problem: storing the unlock state as a plain boolean (m_adminUnlocked)
// allows a memory editor to flip it and bypass all guards without knowing
// the admin password.
//
// Solution: the unlock state is stored as an HMAC-SHA256 token derived from
// a random 32-byte session key generated at process startup. The key never
// leaves memory and is never written to disk or the database.
//
// isUnlocked() verifies the token against the session key on every call.
// Flipping any memory value without also knowing the session key produces
// a verification failure — access is denied.
//
// The local m_adminUnlocked booleans in UI widgets are kept for rendering
// only (icon emoji, wage masking). All actual access guards call isUnlocked().

namespace SessionManager {

// Called once at app startup to initialize the session key.
// Safe to call multiple times — only the first call has effect.
void init();

// Set the admin unlock state. Called by MainWindow::lockAdmin/unlockAdmin.
void setUnlocked(bool unlocked);

// Returns true if the admin session is currently unlocked.
// Verifies the HMAC token — not a simple boolean read.
bool isUnlocked();

} // namespace SessionManager
