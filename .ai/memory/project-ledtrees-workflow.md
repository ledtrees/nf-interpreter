---
name: project-ledtrees-workflow
description: "Раскладка репозиториев LEDTREES, сборка, прошивка и цикл проверки на железе"
metadata:
  node_type: memory
  type: project
  originSessionId: 2930be42-f1cb-4fc7-b5fe-e9ee00a46bfd
---

Общение — на русском; сообщения коммитов и новые комментарии в коде — на английском.

Репозитории: managed-прошивка и инструменты — `ledtrees-esp32`; нативный nanoCLR — `nf-interpreter`; определения сборок (актуальный формат — `assembly.v2.json`) — `ledtrees-assemblies`. Расположение репозиториев брать из текущего workspace.

Сборка managed: `MSBuild.exe nanoframework/ledtrees-device.slnx -p:Configuration=Release -p:BundleId=<guid> -m -nr:false` из корня `ledtrees-esp32` (MSBuild из Visual Studio с nanoFramework Extension; `-nr:false` обязателен — иначе MetadataProcessor держит DLL). После правок managed-кода собирать и говорить, что на железе не проверено.

**Why:** цикл проверки — пользователь прошивает плату задачами VS Code и присылает лог монитора; выводы делать только по этим логам, гипотезы помечать как непроверенные (дважды за сессию «очевидная» гипотеза не подтверждалась логом).

**How to apply:** MAIN прошивать задачей с `-DeployBundle` (кладёт .ltfw в кэш SD — иначе группа не обновится, MAIN пишет warning). SCREEN — обычным deploy. Каждая сборка получает новый BundleId, прошивка и .ltfw должны быть из одного прогона.

Смена interop-сигнатур interoplib (новый нативный метод): править managed `interoplib/LedPixelController.cs` → собрать managed (стабы регенерируются в `Stubs/interoplib`, меняется checksum) → скопировать в `nf-interpreter/InteropAssemblies/interoplib` четыре файла генерата: `interoplib.cpp`, `interoplib.h`, `*_LedPixelController.h`, `*_LedPixelController_mshl.cpp`. НЕ копировать `*_LedPixelController.cpp` (там рукописная реализация) и сохранить ручной блок деклараций (LedTask_*/spi_send_data) в конце `*_LedPixelController.h` — генерат его затирает. После смены checksum натив и managed совместимы только из одного прогона — прошивать парой. Натив собирается `~/.espressif/tools/ninja/1.12.1/ninja.exe -C build` из корня nf-interpreter.
