# ATFWD seccomp compatibility

With OS3.0.306.0.WNCCNXM blobs on LineageOS 24.0, ATFWD-daemon exits with SIGSYS
shortly after startup and init restarts it about every five seconds. A trace
shows the first forbidden syscall is `lseek` while reading the boot ID. The
subsequent SIGABRT reporting path also hits a forbidden syscall; that is not
the original failure.

The extraction fixup appends `lseek: 1` only when absent. All other restrictions
in the vendor policy remain intact. No SELinux change is needed.

Validation: after replacing just this policy, the process remains running and
registers `vendor.qti.hardware.radio.atfwd.IAtFwd/AtFwdAidl`, with SELinux
Enforcing. Recheck stable PID and service registration after a full ROM update.
This does not constitute validation of every AT command or modem feature.
