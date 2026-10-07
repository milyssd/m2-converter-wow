# AzerothCore y WoW 3.3.5a

Esta extensión se ejecuta dentro del **cliente Windows de WoW 3.3.5a, build 12340, de 32 bits**. Se compila como parte de `WarcraftXL.dll`; no es un módulo del servidor AzerothCore. El servidor sigue usando sus objetos, `displayid` y protocolo habituales. Cada cliente que deba ver las colecciones necesita la DLL y los archivos de datos correspondientes.

## Base de integración revisada

Se inspeccionó el núcleo público [Morfium-G/wxl-core](https://github.com/Morfium-G/wxl-core/tree/780dc15b4d330b501970b6354b1f78c749aba818), revisión `780dc15b4d330b501970b6354b1f78c749aba818`. La URL `WarcraftXL/WarcraftXL` incluida en la documentación original no estaba accesible durante esta revisión. Otra versión del núcleo debe conservar las mismas estructuras y eventos antes de considerarse compatible.

El núcleo revisado proporciona `OnItemSlotChange`, `OnItemSlotClear`, `OnUpdate`, `OnM2SkinFinalize`, `OnM2PerFrameUpdate` y `OnBuildBonePalette`. Un detalle relevante de sus hooks es que el evento de equipar se emite **después** de la operación nativa, mientras el evento de desequipar se emite **antes**. La restauración de las otras colecciones debe terminar después del borrado nativo para evitar que este elimine de nuevo sus adjuntos.

## Compilar e instalar en Windows

Requisitos: Visual Studio 2022 con herramientas de C++ para escritorio, Windows SDK, CMake 3.25 o posterior y un cliente build 12340. El `CMakeLists.txt` del núcleo establece C++20 y una biblioteca de ejecución estática. Una configuración x64 normal no genera la DLL que necesita el cliente.

En PowerShell, desde la carpeta que contiene esta extensión:

```powershell
git clone https://github.com/Morfium-G/wxl-core.git WarcraftXL
git -C WarcraftXL checkout 780dc15b4d330b501970b6354b1f78c749aba818
New-Item -ItemType Directory -Force WarcraftXL\scripts\wxl-equip-extension | Out-Null
Copy-Item -Recurse src WarcraftXL\scripts\wxl-equip-extension\src
cmake -S WarcraftXL -B WarcraftXL\build\dll -A Win32
cmake --build WarcraftXL\build\dll --config Release --target WarcraftXL d3d9 --parallel 2
```

Copiar `WarcraftXL\build\dll\Release\WarcraftXL.dll` y `d3d9.dll` junto a `Wow.exe`. En esta revisión del núcleo, el proxy `d3d9.dll` carga `WarcraftXL.dll` desde la primera llamada a `Direct3DCreate9`; no hace falta modificar el ejecutable para esa vía de carga. Evitar combinarlo con otro proxy `d3d9.dll` sin integrar previamente ambos cargadores.

Los puntos de unión 47, 48, 53 y 55 de los ejemplos pueden requerir modelos de personaje HD retroportados. Con modelos originales, usar en `Icon2` un punto realmente presente en el M2 del personaje; tener un servidor AzerothCore no añade esos puntos al cliente. Las colecciones también requieren huesos compatibles y recursos convertidos a Wrath.

El host de archivos de 64 bits es opcional si los M2, skins y BLP están disponibles en MPQ nativos y ya usan un formato que el cliente admite. Si se utilizan carpetas de datos servidas por el host, compilar también `WarcraftXLHost.exe` con `-A x64 -DWXL_BUILD_HOST=ON` y seguir la configuración del núcleo. Esta extensión no convierte por sí sola los formatos M2 modernos.

## Datos del cliente y servidor

- Conservar la estructura de `ItemDisplayInfo.dbc` de 3.3.5a. Los nombres, texturas y opciones de la extensión se escriben en las columnas existentes; no se agregan columnas al DBC.
- Distribuir el DBC modificado como `DBFilesClient\ItemDisplayInfo.dbc`, junto con los M2, sus skins y BLP, por una vía que cargue WarcraftXL o un parche MPQ del cliente.
- Mantener los archivos en `Item\ObjectComponents\<carpeta>\` o `Item\ObjectComponents\collections\`, según el tipo de modelo. Los nombres y sufijos deben coincidir con las opciones del DBC. El formato estándar usa, por ejemplo, `HuM` y `HuF`; la opción `0x40` usa `Hu_M` y `Hu_F`.
- En AzerothCore, el `displayid` de `item_template` debe apuntar al ID correcto. Si se agregan IDs nuevos, incluirlos también en el `ItemDisplayInfo.dbc` que carga el servidor o en su mecanismo de datos equivalente. AzerothCore carga esta tabla; modificar únicamente el cliente no crea el objeto del servidor.
- Las razas personalizadas requieren además compatibilidad en el servidor y sus DBC. Corregir una tabla de creación del cliente no agrega soporte de razas al servidor.

La columna `ClientPrefix` de `ChrRaces.dbc` es una **cadena**, no cuatro caracteres embebidos en el registro. En el registro resuelto del cliente, el campo en `+0x18` se interpreta como un puntero. La definición del build 12340 en [WoWDBDefs](https://github.com/wowdev/WoWDBDefs/blob/master/definitions/ChrRaces.dbd) permite comprobarlo; el comentario `char[4]` del núcleo revisado es impreciso. Para los modelos de equipo el género se representa por `M` o `F` y debe validarse antes de acceder a tablas.

## Fallos reportados y límites de la evidencia

| Reporte original | Evidencia y comprobación necesaria |
| --- | --- |
| [#3: colecciones ausentes en PaperDoll](https://github.com/Morfium-G/wxl-equip-extension/issues/3) | El código recibido descarta el equipo si el nodo del personaje aún es nulo y considera muerto cualquier objeto con ese nodo nulo. La creación o reconstrucción diferida de modelos de interfaz necesita conservar el equipo y reintentarlo. Comprobar el resultado en PaperDoll, inspección y probador, además del personaje en el mundo. |
| [#4: conflicto con races-fix](https://github.com/Morfium-G/wxl-equip-extension/issues/4) | El reporte no incluye reproducción ni registro. Se revisó [Dokman/wxl-character-races-fix](https://github.com/Dokman/wxl-character-races-fix/tree/f28c6f510e4b8716283b48b82165f63f934d29de): sus parches de las tablas de creación no se superponen directamente con los hooks de equipo ni con el almacenamiento de `ChrRaces`. Validar raza, género, prefijo y estado de inicialización elimina accesos inválidos de la extensión, pero no demuestra por sí solo que toda combinación de módulos funcione. |
| [#8: capa elimina tabardo](https://github.com/Morfium-G/wxl-equip-extension/issues/8) | Revisar la correspondencia entre slots internos y slots del inventario, la restauración tras borrados nativos y la conservación de modelos que comparten un attachment point. El propio reporte está marcado para reproducirlo; necesita probarse con ambos modelos reales. |
| Desequipado de colecciones, documentado en el README original | Verificar que quitar una pieza elimina solo sus geosets y mantiene las otras piezas que comparten colección, textura o punto de unión. |

## Comprobación dentro del cliente

Usar un personaje de cada sexo con objetos que tengan modelos conocidos y colecciones convertidas correctamente. Registrar los IDs de objeto y de display, la raza, el modelo, los geosets y `Icon2` para poder repetir cada caso.

1. Equipar y quitar una colección en cada slot soportado; reemplazarla por un objeto sin modelo y por un objeto con otro modelo.
2. Equipar capa y tabardo en ambos órdenes. Repetir con el mismo attachment point, y quitar cada uno manteniendo el otro.
3. Compartir una colección entre dos slots con geosets distintos. Quitar y volver a poner una pieza; comprobar que reaparecen sus triángulos y no cambian los de la otra.
4. Abrir PaperDoll, probador e inspección antes y después de equipar. Cerrar y abrir las ventanas y comprobar los modelos tras su reconstrucción.
5. Cambiar de personaje, reconectar y cambiar de mapa. Comprobar que no quedan colecciones del personaje anterior.
6. Probar las razas estándar de AzerothCore con `races-fix` desactivado y activado. Las razas adicionales necesitan una instalación que realmente las soporte.

Para diagnosticar, activar temporalmente `Log=1` en `[EquipExtension]` de `WTF\WXL\WarcraftXL.ini`, reproducir una sola secuencia y guardar `WarcraftXL_equip.log`. Desactivar después el registro porque puede crecer rápidamente.

Las pruebas de lógica y compilación de código aislado en Linux no sustituyen la compilación Win32 del conjunto ni la comprobación visual en `Wow.exe`. Sin el cliente, los DBC y los modelos del caso reportado, no se puede certificar el comportamiento visual ni declarar reproducido o cerrado el conflicto #4.
