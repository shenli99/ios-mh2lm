# Working on ios-mh2lm

- Read `NOTES.md` before starting; it contains the project goal, hardware facts,
  current blocker, local artifact locations and the next concrete action.
- The user switches agents frequently. Update the active handoff in `NOTES.md`
  after each milestone and before stopping. Record commits, CI run IDs, exact
  build/boot results and unfinished work. Distinguish observations from guesses.
- Commit completed changes. Use `gh` for CircleCI kernel builds and logs (GitHub
  Actions deprecated for this project, CircleCI is the active CI).
  Local single-file cross-compiles are useful before spending a CI run.
- Device bring-up uses `fastboot boot` for testing and boot_a/boot_b for
  persistent boot images. Preserve the Android installation (Android wipes
  iOS root if booted). The user approved (2026-10-15) using internal UFS for
  iOS and said Android may be deleted: the iOS root goes into the existing
  `userdata` partition (no GPT changes). Keep bootloaders, recovery, modem,
  persist and the backups intact, and confirm each destructive step with the
  user before running it.
- Keep device dumps, serial numbers and downloaded Apple artifacts out of git.
