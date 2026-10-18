#!/bin/sh
# Installs the usage collector as a systemd user service on this machine.
# Usage: install-usage-collector.sh DEVICE     (e.g. navrobotec, vallhala, hp)
# The server comes from $TODO_SERVER or ~/.config/todo/server like the todo CLI.
set -e
dev=${1:?usage: $0 DEVICE}
here=$(dirname "$0")
mkdir -p "$HOME/.local/bin" "$HOME/.config/systemd/user"
install -m 755 "$here/usage-collector.py" "$HOME/.local/bin/todo-usage-collector"
sed "s/%i/$dev/" "$here/todo-usage-collector.service" > "$HOME/.config/systemd/user/todo-usage-collector.service"
systemctl --user daemon-reload
systemctl --user enable --now todo-usage-collector.service
