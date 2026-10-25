#!/bin/bash
# Установка / удаление KtxQuickLook для текущего пользователя.
#   ./install.sh            — установить
#   ./install.sh --uninstall
set -euo pipefail
cd "$(dirname "$0")"

NAME="KtxQuickLook.qlgenerator"
DEST_DIR="$HOME/Library/QuickLook"

if [ "${1:-}" = "--uninstall" ]; then
  rm -rf "$DEST_DIR/$NAME"
  qlmanage -r >/dev/null 2>&1 || true
  qlmanage -r cache >/dev/null 2>&1 || true
  echo "Удалено."
  exit 0
fi

if [ ! -d "$NAME" ]; then
  [ -f "$NAME.tar.gz" ] || { echo "Не найден $NAME или $NAME.tar.gz рядом со скриптом"; exit 1; }
  tar xzf "$NAME.tar.gz"
fi

# Quick Look запускается с урезанным PATH, поэтому запоминаем, где лежит ktx СЕЙЧАС.
KTX="${KTX_BIN:-$(command -v ktx || true)}"
if [ -z "$KTX" ]; then
  echo "ktx не найден в PATH. Установите KTX-Software или запустите: KTX_BIN=/путь/к/ktx ./install.sh"
  exit 1
fi
echo "Использую ktx: $KTX ($("$KTX" --version 2>&1 | head -1))"

mkdir -p "$DEST_DIR"
rm -rf "$DEST_DIR/$NAME"
cp -R "$NAME" "$DEST_DIR/"
echo "$KTX" > "$DEST_DIR/$NAME/Contents/Resources/ktx-path.txt"

xattr -dr com.apple.quarantine "$DEST_DIR/$NAME" 2>/dev/null || true
codesign --force --sign - "$DEST_DIR/$NAME"      # после правки Resources подпись надо обновить

qlmanage -r >/dev/null 2>&1 || true
qlmanage -r cache >/dev/null 2>&1 || true
echo "Готово. Проверка:  qlmanage -p /путь/к/файлу.ktx2"
echo "Список генераторов: qlmanage -m plugins | grep -i ktx"
