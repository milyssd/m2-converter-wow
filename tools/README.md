# Limpieza segura de ItemDisplayInfo.dbc (WoW 3.3.5a)

`cleanup_itemdisplayinfo.py` ayuda a corregir las cajas de modelo ausente causadas por nombres de hombreras sobrantes en armaduras del DBC original. Requiere Python 3.10 o posterior; no necesita paquetes adicionales. Funciona con el DBC binario WDBC de **WoW 3.3.5a, build 12340**: 25 campos de 4 bytes por registro. No convierte modelos M2 ni instala WarcraftXL.

El DBC no contiene el tipo de inventario de cada objeto. Exporte **toda** la tabla `item_template` de la base de datos `acore_world` de su servidor AzerothCore como CSV con cabecera:

```sql
SELECT entry, displayid, InventoryType FROM item_template ORDER BY entry;
```

Ejemplo del formato del archivo `item_template.csv`:

```csv
entry,displayid,InventoryType
10001,33635,5
10002,33637,7
```

Use la opción de exportación CSV de su cliente SQL. El archivo debe incluir todos los objetos, también los personalizados, armas y escudos. **Un export parcial puede ocultar usos compartidos y provocar una limpieza incorrecta.** No exporte solamente armaduras.

Primero ejecute una simulación:

```sh
python tools/cleanup_itemdisplayinfo.py ItemDisplayInfo.dbc --mapping item_template.csv
```

Revise los IDs y modelos que aparecen como candidatos. El script solo considera camisa, pecho, cinturón, piernas, pies, muñecas, manos, capa, tabardo y toga (`InventoryType` 4, 5, 6, 7, 8, 9, 10, 16, 19 y 20). Conserva cabezas, hombreras, armas, escudos, objetos sostenidos y cualquier otro tipo. Si un mismo display se utiliza en un tipo protegido y una armadura, lo conserva. Los displays sin correspondencia en el CSV también se conservan.

Conserva los modelos de colección que usan `:` y las filas con configuración de WarcraftXL en `Icon2` que empieza por un número o `:`. **Un modelo personalizado sin estos marcadores no se puede distinguir de un nombre sobrante del DBC original.** Revise sus objetos personalizados antes de escribir. Para limpiar solo IDs comprobados puede repetir `--only-display-id`:

```sh
python tools/cleanup_itemdisplayinfo.py ItemDisplayInfo.dbc --mapping item_template.csv --only-display-id 33635 --only-display-id 33637
```

Después de revisar, añada `--output` con un nombre nuevo:

```sh
python tools/cleanup_itemdisplayinfo.py ItemDisplayInfo.dbc --mapping item_template.csv --only-display-id 33635 --output ItemDisplayInfo.clean.dbc
```

El original se conserva y el destino debe ser distinto y no existir. Se ponen a cero únicamente los offsets de `ModelName_1` y `ModelName_2` de las filas elegidas; se conservan todas las texturas, iconos, geosets, IDs y el bloque de cadenas byte por byte. Los datos sobrantes en el bloque de cadenas se dejan allí para evitar reescrituras innecesarias.

`--allow-custom` permite limpiar filas con marcadores de colección o configuración de `Icon2`; úselo únicamente para IDs revisados y combinándolo con `--only-display-id`. Esta opción no elimina la protección de armas, cabezas, hombreras ni usos compartidos. `--json` produce un informe fácil de guardar o procesar.

Haga una copia de seguridad de su DBC y del parche del cliente. Pruebe el archivo nuevo en su parche de cliente antes de sustituir su versión de trabajo. Esta herramienta no modifica automáticamente el DBC del servidor ni escribe en la base de datos.

Fuentes del formato:

- [AzerothCore DBCfmt.h](https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/shared/DataStores/DBCfmt.h), `ItemDisplayTemplateEntryfmt`.
- [WoWDBDefs ItemDisplayInfo.dbd](https://github.com/wowdev/WoWDBDefs/blob/master/definitions/ItemDisplayInfo.dbd), build `3.3.5.12340`.

Para ejecutar las pruebas de seguridad y conservación binaria:

```sh
python -m unittest discover -s tests -p 'test_cleanup_itemdisplayinfo.py' -v
```
