# Safety Rules — Invariants & Safety Contract

This document defines the non-negotiable invariants for the Xteink X4 firmware.
All contributors and AI coding agents **must** treat these rules as hard constraints.
Violating any rule below is a breaking change regardless of context.

---

## 1. OTA Integrity

- **Never disable OTA.** The over-the-air update path must remain functional in every firmware build, including experimental builds.
- **Never mark firmware valid before all health checks pass.** `esp_ota_mark_app_valid_cancel_rollback()` must only be called after every required health check stage returns `ok`.
- **Prefer pull-based OTA** (device fetches manifest) over push-based remote flashing (inbound connection to device). Pull-based is the default and required approach.
- **Never open inbound ports on the public internet.** OTA and diagnostics are outbound-only from the device's perspective.

## 2. Safe Mode

- **Never remove safe mode.** The physical-button-triggered safe mode entry path must remain intact in every firmware build.
- Safe mode is considered life-support infrastructure. It must survive even when the normal application layer is completely broken.
- Diagnostics code, experimental features, and UI changes must never touch or disable the safe mode entry path.

## 3. Partition Table

- **Never change the partition table without a documented justification.** Any change requires:
  1. A written explanation of why the change is necessary.
  2. Confirmation that both OTA slots still fit within their existing boundaries.
  3. Explicit sign-off in a commit message referencing this rule.
- The two-slot OTA layout (`ota_0`, `ota_1`) must be preserved at all times.

## 4. Bootloader & Hardware Security

- **Never modify bootloader settings.**
- **Never change eFuse settings.**
- **Never enable, disable, or reconfigure secure boot.**
- **Never enable, disable, or reconfigure flash encryption.**
- **Never trigger any irreversible provisioning operation.**
- These settings affect hardware security in ways that cannot be undone. They are outside the scope of this firmware project.

## 5. Secrets & Diagnostics

- **Never expose secrets in any diagnostic output.** The following must always be redacted:
  - Wi-Fi passwords
  - API tokens and bearer tokens
  - Private keys and certificates
  - Signed URLs
  - OTA credentials
- **Never enable dev diagnostics in release builds by default.** All diagnostic flags (`CONFIG_X4_DEV_DIAGNOSTICS`, `CONFIG_X4_AGENT_DIAGNOSTICS`, `CONFIG_X4_VERBOSE_DISPLAY_DIAGNOSTICS`, `CONFIG_X4_DIAG_HTTP_API`) must default to `n` (disabled).
- **Never allow diagnostics code to disable OTA or safe mode.** Diagnostics are read-only observers of firmware state.
- **Never add arbitrary command execution or shell access** to any API endpoint.

## 6. Infrastructure Stability

The following subsystems are classified as **life-support infrastructure**:

- OTA updater (manifest fetch, download, SHA-256 verify, slot write, rollback gate)
- Safe mode (entry detection, minimal Wi-Fi, recovery display, serial logging)
- Health check pipeline (all stages listed in `health-checks.md`)

**Do not refactor or rewrite these subsystems during unrelated firmware changes.**
If a change touches these subsystems, it must be isolated in its own commit/PR with explicit justification.

## 7. Rollback Protection

- The previous working OTA slot must always remain recoverable through rollback whenever possible.
- If ESP-IDF rollback APIs are not available, the equivalent safe behavior must be implemented and the limitation must be documented.

---

## Quick Reference

| Rule | Enforcement |
|------|-------------|
| OTA always enabled | Required in every build |
| Safe mode always present | Required in every build |
| No valid mark before health pass | Code-level gate |
| No partition table change without docs | Review gate |
| No bootloader/eFuse/secure-boot changes | Absolute prohibition |
| No inbound ports | Architecture constraint |
| Secrets always redacted | Code-level gate |
| Diagnostics off in release | Default `n` in Kconfig |
| Infrastructure not refactored incidentally | PR review policy |
