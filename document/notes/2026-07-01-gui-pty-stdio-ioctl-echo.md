# 2026-07-01 GUI PTY stdio ioctl and double echo

## Symptom

BusyBox `sh` now stays alive on the GUI PTY and command I/O works, but typing
commands in the GUI terminal exposed two follow-up problems:

- typing `ls` quickly echoed `ls`;
- typing `ls /` could stall the GUI for a while, with serial output flooded by
  `ECHO_TRACE`.

The first issue looked like "local echo is still too eager", but the trace showed
it was actually double echo.

## Trace Evidence

For the first `ls\n`, the PTY master echoed key input as expected:

```text
pty.master_write from_terminal data="l"
pty.master_read to_terminal data="l"
pty.master_write from_terminal data="s"
pty.master_read to_terminal data="s"
```

After Enter, the shell read the cooked line one byte at a time:

```text
pty.slave_read to_shell data="l"
pty.slave_read to_shell data="s"
pty.slave_read to_shell data="\n"
```

Then BusyBox wrote the same command bytes back to the PTY:

```text
pty.slave_write from_shell data="l"
pty.slave_write from_shell data="s"
pty.slave_write from_shell data="\n"
pty.master_read to_terminal data="ls\n"
```

That proves the visible duplicate is not a GUI repaint bug.  The PTY line
discipline had `ECHO` enabled, while BusyBox's line editor also echoed the line.

## Root Cause

`sys_ioctl` treated fd 0/1/2 as legacy console fds before consulting the current
`FDTable`.

That was valid for the old console-only path, but it is wrong for GUI shells:
their stdin/stdout/stderr are real `File` objects pointing at the PTY slave
inode.  BusyBox probes and updates terminal mode through stdio ioctls:

- `TCGETS` reads termios;
- `TCSETS` disables canonical PTY echo when BusyBox's line editor owns echo.

Because fd 0/1/2 were short-circuited to the global console TTY, `TCSETS` never
reached `PtySlaveOps::ioctl`.  The PTY therefore kept `ECHO` enabled, and the
shell's own line editor added a second echo.

## Fix

`sys_ioctl` now dispatches through an installed fd-table `File` first, including
fds 0/1/2.  Only absent legacy stdio fds fall back to `console_ioctl`.

This preserves the old boot/test console behavior while letting GUI PTY slave
ioctls reach the correct inode.

Temporary `ECHO_TRACE` logs on the PTY slave ioctl path print `TCGETS/TCSETS`
`c_lflag`, so the next GUI run can confirm that BusyBox's termios writes reach
the PTY.

## Stall Root Cause

The `ls /` stall was amplified by diagnostic logging in the render-frame hot
path:

```text
host.render_frame frame_count=...
```

That emitted once per frame and flooded serial output.  Serial logging is slow
enough to delay the GUI and hide useful PTY output.  The per-frame trace was
removed; future GUI diagnostics should log state transitions or I/O events, not
every frame.

## Regression Guard

- stdio fds may be real fd-table entries.  Never assume fd 0/1/2 mean "console"
  before checking `current_fd_table()`.
- PTY echo bugs must be debugged at three points: master input echo,
  slave-read cooked input, and slave-write program output.
- Hot-path GUI traces must be bounded.  Render-loop logs can create the very
  stall being investigated.

## Validation Status

No kernel test was run for this quick debug pass, per operator request.  The
next interactive GUI check should look for:

- `pty.slave_ioctl TCSETS lflag=...` after BusyBox starts line editing;
- no second `pty.master_read ... data="ls\n"` caused by shell-side echo unless
  BusyBox intentionally echoes in that mode;
- no per-frame `host.render_frame frame_count` flood.
