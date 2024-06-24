MONITOR_RESET="monitor reset"

if [ "$1" == "c" ]; then
  MONITOR_RESET=
  echo "Attaching without reset"
fi

cat >debug/gdbinit <<EOF
define hook-quit
  set confirm off
end
define hook-run
  set confirm off
end
define hookpost-run
  set confirm on
end
set pagination off
target extended-remote localhost:3333

b swv_trap_line
commands
  silent
  printf "%s\n", (char *)swv_buf
  c
end
${MONITOR_RESET}
c
EOF

~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-gdb build/app.elf -x debug/gdbinit
rm debug/gdbinit
