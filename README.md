# wxl-equip-extension — AzerothCore / WoW 3.3.5a

Código fuente corregido para el cliente **Windows de 32 bits, compilación 12340**, integrado en WarcraftXL y utilizado con un servidor AzerothCore. El paquete no contiene una DLL compilada ni modelos de juego.

- [Compilación, instalación y compatibilidad con AzerothCore](docs/COMPATIBILIDAD_AZEROTHCORE.md).
- [Cambios y estado de los reportes](docs/CAMBIOS_Y_PRUEBAS.md).
- [Limpieza opcional de nombres sobrantes en el DBC](tools/README.md).

El núcleo público revisado es [Morfium-G/wxl-core](https://github.com/Morfium-G/wxl-core), revisión `780dc15b4d330b501970b6354b1f78c749aba818`. Copiar esta extensión en `scripts/wxl-equip-extension/` de ese núcleo y compilar allí con Visual Studio para **Win32**. No colocar el código en `azerothcore/modules`.

Las pruebas automatizadas verifican la lógica y los datos con un motor simulado. La compilación completa de la DLL y la comprobación visual deben realizarse en Windows. El conflicto #4 con `wxl-character-races-fix` aún necesita una reproducción real.

---

Un módulo de [WarcraftXL](https://github.com/Morfium-G/wxl-core) que amplía las posibilidades de `ItemDisplayInfo.dbc`. Extiende el uso de modelos M2 adjuntos a las ranuras de equipo compatibles, añade un segundo modelo por ranura e incorpora colecciones M2 con filtros por geoset. Todo se configura mediante datos del DBC, sin cambiar el código del cliente para cada objeto.

---

## Requisitos

- **WarcraftXL** instalado en la carpeta del cliente WoW 3.3.5a (proxy `d3d9.dll` y `WarcraftXL.dll`).
- Este módulo se compila automáticamente dentro de `WarcraftXL.dll`. No necesita una DLL independiente ni archivos adicionales propios del módulo para ejecutarse.

---

## Compilación

CMake detecta automáticamente los archivos del módulo al compilar WarcraftXL. Para incluirlo, clona o copia su carpeta en `scripts/wxl-equip-extension/`, dentro del código fuente de WarcraftXL, y compila de la forma habitual:

```powershell
$env:PATH = "C:\path\to\cmake\bin;$env:PATH"
.\build.ps1
```

Copia los archivos generados, `WarcraftXL.dll` y `d3d9.dll`, en la carpeta de tu cliente WoW.

---

## Cambios en `ItemDisplayInfo.dbc`

### Ranuras compatibles

El cliente original 3.3.5a solo admite `ModelName_1`, `ModelName_2`, `ModelTexture_1` y `ModelTexture_2` para cabeza y hombros. Este módulo amplía ese soporte a las ranuras de la tabla siguiente. Las ranuras que no aparecen —cuello, anillos, abalorios y armas— todavía no están implementadas.

| Ranura | Modelo 1 | Modelo 2 | Textura 1 | Textura 2 | Configuración de Icon2 |
|------|--------|--------|----------|----------|----------------|
| Cabeza | soporte parcial | **añadido** | - | **añadido** | **añadido** |
| Hombros | soporte parcial | soporte parcial | -  | - | **añadido** |
| Camisa | **nuevo** | **nuevo** | **nuevo** | **nuevo** | **nuevo** |
| Pecho | **nuevo** | **nuevo** | **nuevo** | **nuevo** | **nuevo** |
| Cintura | **nuevo** | **nuevo** | **nuevo** | **nuevo** | **nuevo** |
| Piernas | **nuevo** | **nuevo** | **nuevo** | **nuevo** | **nuevo** |
| Pies | **nuevo** | **nuevo** | **nuevo** | **nuevo** | **nuevo** |
| Muñecas | **nuevo** | **nuevo** | **nuevo** | **nuevo** | **nuevo** |
| Manos | **nuevo** | **nuevo** | **nuevo** | **nuevo** | **nuevo** |
| Espalda | **nuevo** | **nuevo** | **nuevo** | **nuevo** | **nuevo** |
| Tabardo | **nuevo** | **nuevo** | **nuevo** | **nuevo** | **nuevo** |

Cada objeto equipado puede adjuntar hasta **dos modelos M2 independientes** al personaje o PNJ que lleve esa armadura.

---

### `ModelName_1` y `ModelName_2`: filtros de geosets y colecciones

Los campos de nombre de modelo siguen funcionando como antes para los modelos simples de cada ranura. Se añaden las siguientes posibilidades:

#### Colecciones M2

Añade dos puntos y una lista de IDs de geoset separados por comas para activar una colección:

```
chestplate_paladin_tier2.mdx:1201,2301
```

- El carácter `:` cambia la ruta de búsqueda del modelo: pasa de la carpeta de la ranura (`Item\ObjectComponents\<SlotName>\`) a la carpeta compartida de colecciones (`Item\ObjectComponents\collections\`). Esta organización coincide con la del cliente moderno, por lo que permite conservar los nombres de las colecciones exportadas.
- Los IDs que aparecen después de `:` son los **únicos** geosets que se muestran. Los demás se ocultan. El geoset `0`, correspondiente a la geometría siempre visible, nunca se elimina mediante el filtro.
- Combínalo con la **bandera `0x40`**, que activa el nuevo formato de sufijo, si la colección utiliza la convención moderna `Race_Gender`.

#### Modelos estándar, sin colección

Si el nombre no contiene dos puntos, el modelo se busca en la carpeta habitual de su ranura. Se añaden los sufijos de raza y género según las banderas de `Icon2`, que se describen más adelante.

---

### `ModelTexture_1` y `ModelTexture_2`

Estas columnas funcionan igual que antes para cabeza y hombros. Su formato no cambia; ahora también se utilizan en las demás ranuras compatibles.

---

### `Icon2`: configuración de puntos de unión y banderas

El campo `Icon2` no se utiliza en el cliente original 3.3.5a. Esta extensión lo aprovecha para configurar cómo se cargan y se adjuntan los modelos 1 y 2.

**Formato:** `<model1_attachment>:<model2_attachment>:<flags>:<customfolder>`

Los IDs de los puntos de unión se limitan a `0..60`. El valor `4294967295` desactiva el canal de modelo correspondiente. Las banderas admiten valores decimales o hexadecimales con el prefijo `0x`. El cuarto campo, opcional, selecciona una carpeta relativa dentro de `Item\ObjectComponents\`. Al utilizar una carpeta personalizada o la bandera `0x20`, las rutas de texturas de tipo 0 escritas dentro del M2 se redirigen a la carpeta del modelo en su copia virtual. Los archivos originales no se modifican.

Todos los campos son opcionales. Deja un valor vacío para conservar la configuración predeterminada de la ranura. Ejemplos:

| Valor de Icon2 | Significado |
|-------------|---------|
| *(vacío)* | Usa los puntos de unión y las banderas predeterminados de la ranura |
| `11:55` | Modelo 1 en el punto 11 y modelo 2 en el punto 55 |
| `:34` | Modelo 2 en el punto 34; el modelo 1 conserva el punto predeterminado |
| `::6` | Banderas = `0x4 \| 0x2`: sin sufijos de género ni raza; puntos de unión predeterminados |
| `19:19` | Ambos modelos en el punto 19 (`Ground_Base`, base del personaje) |
| `::96` | Banderas = `0x20 \| 0x40`: subcarpeta y nuevo formato de sufijo; puntos predeterminados |

#### Banderas

| Bandera | Nombre | Efecto |
|------|------|--------|
| `0x1` | Reservada | El evento disponible se ejecuta después de aplicar los geosets originales del personaje y no los suprime. Esta bandera no está implementada; configura por separado las columnas habituales de geosets del DBC. |
| `0x2` | Sin sufijo de género | No añade `_M` ni `_F` a la ruta del modelo. Úsala en modelos comunes a ambos géneros. Combínala con `0x4` para omitir ambos sufijos y su separador `_`. |
| `0x4` | Sin sufijo de raza | No añade el código de raza a la ruta del modelo. Combínala con `0x2` para omitir ambos sufijos. |
| `0x8` | Añadir raza a la textura | Añade el código de raza a la ruta de la textura. Normalmente las texturas no llevan este sufijo. |
| `0x10` | Añadir género a la textura | Añade el código de género a la ruta de la textura. |
| `0x20` | Modelo en subcarpeta | Coloca el modelo en una subcarpeta cuyo nombre coincide con el del modelo base, sin sufijos. Por ejemplo, `chestplate_paladin_tier2_HuF.mdx` se busca en `Item\ObjectComponents\Chest\chestplate_paladin_tier2\chestplate_paladin_tier2_HuF.mdx`. Las texturas usan la misma subcarpeta. Facilita organizar, distribuir y combinar modelos. |
| `0x40` | Nuevo formato de sufijo | Usa la convención del cliente moderno, con un guion bajo adicional entre raza y género: `Hu_F` en lugar de `HuF`. Solo tiene efecto cuando se añaden ambos sufijos: en el modelo, si no se desactivaron con `0x2` y `0x4`; en la textura, si están activadas `0x8` y `0x10`. Permite conservar los nombres de los modelos exportados. |

---

## Puntos de unión predeterminados por ranura

Si no se indica un punto de unión en `Icon2`, se utilizan los siguientes valores:

| Ranura | Punto del modelo 1 | Punto del modelo 2 |
|------|-------------------|-------------------|
| Cabeza | 11 (Casco) | 55 (Parte superior de la cabeza) |
| Hombros | 6 (Hombro izquierdo) | 5 (Hombro derecho) |
| Camisa | 34 (Pecho) | 34 (Pecho) |
| Pecho | 34 (Pecho) | 34 (Pecho) |
| Cintura | 53 (Hebilla del cinturón) | 53 (Hebilla del cinturón) |
| Piernas | 9 (Cadera derecha) | 10 (Cadera izquierda) |
| Pies | 47 (Pie izquierdo) | 48 (Pie derecho) |
| Muñecas | 3 (Codo derecho) | 4 (Codo izquierdo) |
| Manos | 1 (Mano derecha) | 2 (Mano izquierda) |
| Espalda | 12 (Espalda) | 12 (Espalda) |
| Tabardo | 34 (Pecho) | 34 (Pecho) |

---

## Filas de ejemplo del DBC: conjunto de armadura de Sentencia

Los siguientes ejemplos muestran un conjunto de Sentencia en dos colores, implementado con este módulo. Varias piezas comparten una colección M2 (`plate_raidpaladint2_d_01_chest.mdx`) y filtran sus geosets para mostrar solo la geometría correspondiente a cada ranura. Los modelos de colección se encuentran en `Item\ObjectComponents\collections\palat2\`. El cinturón utiliza un modelo independiente y la bandera de subcarpeta `0x20` para mantener sus archivos en una carpeta propia.

Estos ejemplos muestran distintas formas de utilizar el módulo; no pretenden establecer una única configuración recomendada.

### Sentencia: color original

| ID | Ranura | Model1 | Model2 | ModelTexture1 | ModelTexture2 | Icon1 | Icon2 |
|----|------|--------|--------|---------------|---------------|-------|-------|
| 45888 | Cabeza | `palat2\plate_raidpaladint2_d_01_helm.mdx` | | `palat2\plate_raidpaladint2_d_01_helm_be_m_6037071` | | inv_plate_raidpaladint2_d_01_HELM | |
| 34258 | Hombros | `palat2\plate_raidpaladint2_d_01_shoulder_l.mdx` | `palat2\plate_raidpaladint2_d_01_shoulder_r.mdx` | `palat2\plate_raidpaladint2_d_01_shoulder_l_6037139` | `palat2\plate_raidpaladint2_d_01_shoulder_l_6037139` | inv_plate_raidpaladint2_d_01_shoulder | |
| 33635 | Pecho | `palat2\plate_raidpaladint2_d_01_chest.mdx:2201` | | `palat2\plate_raidpaladint2_d_01_chest_be_m_6037063` | | inv_plate_raidpaladint2_d_01_chest | `19` |
| 33633 | Cinturón | `plate_raidpaladint2_d_01_belt.mdx` | | `plate_raidpaladint2_d_01_belt_6037176` | | inv_plate_raidpaladint2_d_01_belt | `53::38` |
| 33634 | Brazales | | | | | inv_plate_raidpaladint2_d_01_bracer | |
| 33636 | Guantes | `palat2\plate_raidpaladint2_d_01_chest.mdx:401` | `palat2\plate_raidpaladint2_d_01_chest.mdx:2301` | `palat2\plate_raidpaladint2_d_01_chest_be_m_6037063` | `palat2\plate_raidpaladint2_d_01_chest_be_m_6037063` | inv_plate_raidpaladint2_d_01_glove | `19:19` |
| 33637 | Piernas | `palat2\plate_raidpaladint2_d_01_chest.mdx:1301` | | `palat2\plate_raidpaladint2_d_01_chest_be_m_6037063` | | inv_plate_raidpaladint2_d_01_pant | `19` |
| 33639 | Botas | `palat2\plate_raidpaladint2_d_01_chest.mdx:2001` | | `palat2\plate_raidpaladint2_d_01_chest_be_m_6037063` | | inv_plate_raidpaladint2_d_01_boot | `19` |

### Sentencia: variante violeta

| ID | Ranura | Model1 | Model2 | ModelTexture1 | ModelTexture2 | Icon1 | Icon2 |
|----|------|--------|--------|---------------|---------------|-------|-------|
| 42853 | Hombros | `palat2\plate_raidpaladint2_d_01_shoulder_l.mdx` | `palat2\plate_raidpaladint2_d_01_shoulder_r.mdx` | `palat2\plate_raidpaladint2_d_01_shoulder_purple` | `palat2\plate_raidpaladint2_d_01_shoulder_purple` | INV_Shoulder_37 | |
| 42863 | Pecho | `palat2\plate_raidpaladint2_d_01_chest.mdx:2201` | | `palat2\plate_raidpaladint2_d_01_chest_purple` | | INV_Chest_Plate11 | `19` |
| 42862 | Cinturón | `plate_raidpaladint2_d_01_belt.mdx` | | `plate_raidpaladint2_d_01_belt_purple` | | INV_Belt_23 | `53::38` |
| 42851 | Brazales | | | | | INV_Bracer_13 | |
| 43639 | Guantes | `palat2\plate_raidpaladint2_d_01_chest.mdx:401` | `palat2\plate_raidpaladint2_d_01_chest.mdx:2301` | `palat2\plate_raidpaladint2_d_01_chest_purple` | `palat2\plate_raidpaladint2_d_01_chest_purple` | INV_Gauntlets_09 | `19:19` |
| 42859 | Piernas | `palat2\plate_raidpaladint2_d_01_chest.mdx:1301` | | `palat2\plate_raidpaladint2_d_01_chest_purple` | | INV_Chest_Cloth_59 | `19` |
| 42864 | Botas | `palat2\plate_raidpaladint2_d_01_chest.mdx:2001` | | `palat2\plate_raidpaladint2_d_01_chest_purple` | | INV_Boots_Chain_08 | `19` |

**Notas sobre el conjunto:**

- **Pecho, piernas, botas y guantes** cargan sus geosets desde el mismo archivo de colección. El punto `19` (`Ground_Base`) los sitúa respecto a la raíz del personaje, en lugar de un hueso concreto. En este ejemplo, la colección M2 ya incorpora las transformaciones de los huesos.
- **Los guantes** se dividen en dos grupos de geosets, `401` y `2301`, repartidos entre los modelos 1 y 2. Ambos utilizan `19:19`, lo que permite asignar una textura o controlar la visibilidad de cada parte por separado.
- **Las hombreras** utilizan dos archivos M2: uno para la izquierda (`_l`) y otro para la derecha (`_r`). No necesitan configuración en `Icon2`: los valores predeterminados de la ranura asignan el punto 6, izquierdo, al modelo 1 y el punto 5, derecho, al modelo 2.
- **El cinturón** utiliza `Icon2` con el valor `53::38`: punto 53, correspondiente a la hebilla, sin un segundo modelo en el ejemplo y con las banderas `0x26` (`0x20 | 0x04 | 0x02`). El modelo está en su propia subcarpeta y no lleva sufijos de raza ni género.
- **Los brazales** no tienen modelos definidos. Su geometría procede de los geosets propios del personaje y no necesita un M2 adjunto.

---

## DBC originales y comprobaciones pendientes

Muchas filas originales de 3.3.5a contienen nombres de modelos sobrantes en armaduras que no los utilizaban. La extensión puede intentar cargarlos y mostrar cajas de modelo ausente. La herramienta [cleanup_itemdisplayinfo.py](tools/README.md) permite revisar y limpiar esos nombres a partir de una exportación completa de `item_template`, conservando los IDs de visualización de armas, cabezas, hombreras y modelos personalizados marcados. No modifica el DBC automáticamente.

Se corrigieron la correspondencia capa/tabardo, la reconstrucción después de quitar equipo, la conservación de referencias compartidas, el equipo pendiente en modelos de interfaz, el aislamiento de filtros/texturas y las rutas de texturas internas. Ver [estado de cada reporte y pruebas pendientes](docs/CAMBIOS_Y_PRUEBAS.md). El resultado visual y la combinación con otros módulos deben comprobarse en el cliente.

Los modelos virtualizados deben ser M2 **MD20/v264** de Wrath, con todos sus archivos de tipo .skin declarados disponibles. No se admiten archivos MD21 del cliente moderno sin convertir. Cada filtro y cada grupo de modelos compartidos admite hasta 16 IDs de geoset. Un sufijo de colección vacío o mal formado se rechaza.

---

## Planes futuros

Añadir compatibilidad con las ranuras restantes, como cuello, anillos y abalorios.

---

## Referencia de puntos de unión

Los siguientes IDs corresponden a puntos de unión presentes en modelos de personaje HD adaptados al cliente antiguo:

```c
typedef enum<uint32> {
    AT_Shield_MountMain_ItemVisual0 = 0,
    AT_HandRight_ItemVisual1        = 1,
    AT_HandLeft_ItemVisual2         = 2,
    AT_ElbowRight_ItemVisual3       = 3,
    AT_ElbowLeft_ItemVisual4        = 4,
    AT_Right_Shoulder               = 5,
    AT_Left_Shoulder                = 6,
    AT_Right_Knee                   = 7,
    AT_Left_Knee                    = 8,
    AT_HipRight                     = 9,
    AT_HipLeft                      = 10,
    AT_Helmet                       = 11,
    AT_Back                         = 12,
    AT_ShoulderFlapRight            = 13,
    AT_ShoulderFlapLeft             = 14,
    AT_Bust_ChestBloodFront         = 15,
    AT_Bust2_ChestBloodBack         = 16,
    AT_Face_Breath                  = 17,
    AT_AboveChar_PlayerName         = 18,
    AT_Ground_Base                  = 19,
    AT_Top_of_Head                  = 20,
    AT_SpellLeftHand                = 21,
    AT_SpellRightHand               = 22,
    AT_Special1                     = 23,
    AT_Special2                     = 24,
    AT_Special3                     = 25,
    AT_SheathMainHand               = 26,
    AT_SheathOffHand                = 27,
    AT_SheathShield                 = 28,
    AT_Belly_PlayerNameMounted      = 29,
    AT_LargeWeaponLeft              = 30,
    AT_LargeWeaponRight             = 31,
    AT_HipWeaponLeft                = 32,
    AT_HipWeaponRight               = 33,
    AT_Chest                        = 34,
    AT_HandArrow                    = 35,
    AT_Bullet                       = 36,
    AT_DemolisherVehicle1           = 37,
    AT_DemolisherVehicle2           = 38,
    AT_Vehicle_Seat1                = 39,
    AT_Vehicle_Seat2                = 40,
    AT_Vehicle_Seat3                = 41,
    AT_Vehicle_Seat4                = 42,
    AT_Vehicle_Seat5                = 43,
    AT_Vehicle_Seat6                = 44,
    AT_Vehicle_Seat7                = 45,
    AT_Vehicle_Seat8                = 46,
    AT_LeftFoot                     = 47,
    AT_RightFoot                    = 48,
    AT_ShieldNoGlove                = 49,
    AT_SpineLow                     = 50,
    AT_AlteredShoulderR             = 51,
    AT_AlteredShoulderL             = 52,
    AT_BeltBuckle                   = 53,
    AT_SheathCrossbow               = 54,
    AT_HeadTop                      = 55,
    AT_VirtualSpellDirected         = 56,
    AT_Backpack                     = 57,
    AT_Unk_58                       = 58,
    AT_Unk_59                       = 59,
    AT_Unk_60                       = 60,
} ATTACHMENT_ID;
```

## Configuración

En la carpeta `wtf\WXL\` se encuentra el archivo INI que permite activar o desactivar el registro de diagnóstico.
