# Cambios para WoW 3.3.5a y AzerothCore

Se trabajó sobre el ZIP aportado, conservando los avisos GPL y la implementación como módulo del cliente. No se enviaron cambios al repositorio de otra persona.

## Reportes revisados

| Reporte | Cambio aplicado | Estado |
| --- | --- | --- |
| [#2: rutas internas y subcarpetas](https://github.com/Morfium-G/wxl-equip-extension/issues/2) | Reescritura de descriptores de texturas hardcoded en copias virtuales del M2, respetando la carpeta resuelta. El flag actual de subcarpeta es `0x20`; el reporte histórico menciona `0x40`, que en este ZIP corresponde al sufijo moderno. | Pruebas de datos y rutas aprobadas; resultado visual pendiente. |
| [#3: PaperDoll sin colecciones](https://github.com/Morfium-G/wxl-equip-extension/issues/3) | Conservación del equipo recibido antes de crear el nodo, reintento en `OnUpdate` y remapeo de paletas para colecciones sin depender del flag de la escena del mundo. | Pruebas de eventos aprobadas; PaperDoll, inspección y probador pendientes en cliente. |
| [#4: conflicto con races-fix](https://github.com/Morfium-G/wxl-equip-extension/issues/4) | Lecturas protegidas de DBC, límites de raza/género y sufijos `M/F`. Se compararon las direcciones de los parches públicos de ambos proyectos. | No se encontró solapamiento directo. Falta reproducción del conflicto; no se declara resuelto. |
| [#8: capa sustituye tabardo](https://github.com/Morfium-G/wxl-equip-extension/issues/8) | Corrección de slots internos, reconstrucción completa fuera del borrado nativo y claves independientes por punto de anclaje y textura. | Pruebas de eventos y caché aprobadas; combinación de modelos reales pendiente. |
| [#9: tabardo queda al quitarlo](https://github.com/Morfium-G/wxl-equip-extension/issues/9) | Capa: slot interno 10/inventario 14. Tabardo: interno 9/inventario 18. Los adjuntos retirados permanecen en una cola de limpieza incluso al quitar la última entrada. | Pruebas de retirada y limpieza aprobadas; resultado visual pendiente. |
| DBC originales con nombres sobrantes | Herramienta de revisión y limpieza con CSV completo de AzerothCore, simulación predeterminada y escritura exclusiva a un archivo nuevo. | 17 pruebas de conservación binaria, selección y protección de archivos aprobadas. |

## Otras correcciones necesarias

- Rutas y campos numéricos acotados, sin desbordamientos ni truncamientos silenciosos.
- Carpetas anidadas sin repetir prefijos y texturas `.blp` sin duplicar extensión.
- Filtros estrictos; configuración inválida no carga una colección completa por accidente.
- Claves de modelos normales y colecciones aisladas por personaje, modelo, textura completa, filtro y attachment point.
- Referencias de los grupos compartidos retenidas antes de quitar el equipo. La recuperación de un contexto inválido reconstruye todo el personaje.
- Publicación de todas las skins declaradas como una unidad; formato MD20/v264 requerido.
- Bloqueo de la tabla del proveedor para llamadas concurrentes de los hilos de carga; la lectura de archivos queda fuera del mutex para evitar reentrada bloqueada.
- El remapeo de huesos se limita a colecciones; los modelos normales mantienen sus propias animaciones.

## Ejecutar pruebas

Desde la carpeta de la extensión, en Linux con Bash, g++ y Python 3.10 o posterior:

```sh
bash tools/run_tests.sh
bash tools/run_tests.sh --sanitize
```

En la validación actual pasaron 15 casos de eventos del equipo y 17 pruebas Python de DBC, además de los conjuntos de analizadores/rutas y proveedor virtual. AddressSanitizer/UndefinedBehaviorSanitizer aprobaron los conjuntos C++; ThreadSanitizer aprobó la prueba concurrente del proveedor.

El conjunto compila los helpers y el proveedor, y también el `EquipExtension.cpp` de producción contra un motor simulado. Los tests conservan las aserciones y verifican referencias, orden de eventos, filtros, separación de instancias y bytes de DBC. Los stubs no sustituyen una comprobación del ABI ni del renderizado Windows.

La prueba concurrente del proveedor también puede ejecutarse con ThreadSanitizer:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Wno-unknown-pragmas -pthread -g -fsanitize=thread -Isrc -Itests/stubs src/VirtualPath.cpp tests/virtual_path_test.cpp -o /tmp/wxl-vpath-tsan
/tmp/wxl-vpath-tsan
```

## Límites que deben respetarse

El núcleo revisado no publica un evento de destrucción del objeto de personaje de la interfaz. Un nodo ausente conserva metadatos hasta 30 segundos para permitir su inicialización o reconstrucción, y después expira sin volver a tocar el nodo antiguo. Esto limita cachés y estado abandonados, pero no equivale a un identificador de generación del CMO; la destrucción exacta y reutilización de direcciones requiere soporte adicional del núcleo. Las pruebas de cambio de personaje y reconexión siguen siendo necesarias.

La API disponible se ejecuta después de aplicar los geosets originales: el flag `0x1` de ocultarlos no está implementado. Los valores `a|b` de attachment se aceptan para conservar el formato, pero esta extensión usa el lado izquierdo; no añade soporte de slots laterales ni de razas personalizadas al servidor.

Este entorno Linux no dispone del cliente WoW, Windows SDK ni MSVC. No se generó ni certificó una DLL Win32. Los pasos de compilación Windows se basan en el CMake del núcleo público indicado en la guía y deben ejecutarse allí. No se incluyeron DBC ni recursos de juego.
