#!/bin/bash
# Launches Socket with any flags listed in
# ${XDG_CONFIG_HOME:-~/.config}/socket-flags.conf (one or more per line, `#`
# starts a comment), the way Arch's Chromium and Brave packages do.

flags_file="${XDG_CONFIG_HOME:-$HOME/.config}/socket-flags.conf"
flags=()
if [[ -f $flags_file ]]; then
  while IFS= read -r line || [[ -n $line ]]; do
    line="${line%%#*}"
    read -ra words <<< "$line"
    flags+=("${words[@]}")
  done < "$flags_file"
fi

exec /opt/socket/socket-browser "${flags[@]}" "$@"
