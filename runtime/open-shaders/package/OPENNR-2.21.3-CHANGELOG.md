# OpenNR 2.21.3 candidate

## Allow the periphery hybrid at non-aligned display heights

- Remove the unnecessary display-height multiple-of-8 requirement from the whole-eye Neural Rendering periphery route; retain the per-eye width alignment requirement.
- Log periphery route activation with display size and settings, and log the first route gate refusal reason only when that status changes.
- Extend the runtime-include shader test to dispatch the hybrid compositor at heights 2428 and 2432.

This candidate does not alter display sizing, the NR-only path, effect shaping, or any user profile. In-game behavior and performance remain unverified until the next user test.
