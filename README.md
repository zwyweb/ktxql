# KtxQuickLook

Quick Look-плагин (`.qlgenerator`) для KTX2-текстур: превью по пробелу и иконки в Finder,
по духу как [WebPQuickLook](https://github.com/emin/WebPQuickLook). macOS 10.15+, x86_64.

Сам декодинг делает ваш `ktx` (KTX-Software 4.4.x): плагин вызывает `ktx extract`
и показывает результат. Поддерживается всё, что умеет `ktx extract`: несжатые форматы,
BasisLZ/ETC1S, UASTC, ASTC, HDR (EXR). **KTX1 не поддерживается** — это ограничение `ktx extract`.

## Сборка
Push в репозиторий → GitHub Actions (`.github/workflows/build.yml`) → артефакт
`KtxQuickLook-macos-x86_64` (`KtxQuickLook.qlgenerator.tar.gz` + `install.sh`).
Тег `v*` дополнительно создаёт Release.

## Установка
```sh
tar xzf KtxQuickLook.qlgenerator.tar.gz   # если ещё не распаковано
./install.sh                              # ktx берётся из PATH и запоминается в плагине
qlmanage -p some.ktx2                     # проверка
```
Удаление: `./install.sh --uninstall`. Другой `ktx`: `KTX_BIN=/path/to/ktx ./install.sh`.

Если превью не появилось: `mdls -name kMDItemContentType some.ktx2`. Если тип не `org.khronos.ktx2`
или `org.khronos.ktx`, добавьте его в `LSItemContentTypes` в `Info.plist` и пересоберите.

## Как ограничено потребление ресурсов
| Что | Как |
|---|---|
| Размер картинки | Выбирается mip-уровень: иконка — наименьший ≥ размера иконки, превью — ≤ 2048 px |
| Лимиты | иконка: ≤ 8 Мпикс и ≤ 128 МБ; превью: ≤ 24 Мпикс и ≤ 512 МБ; сверху — без превью |
| Параллелизм | не более 2 процессов `ktx` на всю систему (flock), приоритет nice 10 |
| Таймаут | иконка 8 с, превью 20 с, затем `ktx` убивается; отмена запроса тоже убивает |
| Кэш | `~/…/C/ktxquicklook`, ключ = путь+размер+mtime+уровень, до 256 МБ / 1500 файлов, LRU |

Все пороги — `#define` в `src/ktxcore.h` (переопределяются через `-D`).

## Тест ядра без macOS
```sh
cc -std=c99 -D_DEFAULT_SOURCE -o test_core src/ktxcore.c test/test_core.c
KTXQL_BIN=$(command -v ktx) ./test_core texture.ktx2 thumb 256
```
