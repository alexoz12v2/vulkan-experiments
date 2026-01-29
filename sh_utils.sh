
# disable escapes when in a TTY
if [[ -t 1 ]]; then
  tty_escape() { printf "\033[%sm" "$1"; }
else
  tty_escape() { :; }
fi

tty_bold() { tty_escape "1" }
tty_reset() { tty_escape "0" }
tty_yellow() { tty_escape "33" }
tty_red() { tty_escape "31" }
tty_bold_red() { tty_escape "1;31" }
tty_bold_yellow() { tty_escape "1;33" }

# ${} variable expansion, ${@:2} $@ = all arguments, :2 = start from 2
print_err() {
  printf "%s%s%s%s\n" \
    "$(tty_bold_red)" "$1" \
    "$(tty_red)${@:2}" \
    "$(tty_reset)"
}

print_info() {
  printf "%s%s%s%s\n" \
    "$(tty_bold)" "$1" \
    "$(tty_reset)${@:2}" \
    "$(tty_reset)"
}

print_warn() {
  printf "%s%s%s%s\n" \
    "$(tty_bold_yellow)" "$1" \
    "$(tty_yellow)${@:2}" \
    "$(tty_reset)"
}

