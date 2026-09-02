# Contributing

Changes must preserve damage, fail open to vanilla impact behavior when state is
ambiguous, and never introduce modal dialogs or desktop focus changes. Add or
update deterministic policy tests for every behavioral change. Runtime hooks
must have an exact executable-version gate and a checked instruction signature.

Run `tools/build.bat` before submitting a change. Do not commit build outputs,
downloaded game files, private keys, logs, or user configuration.
