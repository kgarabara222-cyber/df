# Сборка через телефон

В проект уже добавлен GitHub Actions workflow `.github/workflows/build.yml`.

1. Создай пустой репозиторий на GitHub.
2. Загрузи в него ВСЕ файлы и папки из этого проекта, включая `.github/workflows/build.yml`.
3. Открой вкладку Actions.
4. Запусти `Build CYD Headless`.
5. После завершения открой готовый run и скачай artifact `cyd-headless-firmware`.
6. Внутри будут `firmware.bin`, `bootloader.bin` и `partitions.bin`.

Не переименовывай бинарники и не стирай Flash без необходимости.
