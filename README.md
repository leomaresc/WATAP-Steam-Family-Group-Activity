# Steam Recent Play

Visor **nativo y ultraligero** para Windows que muestra la actividad reciente de Steam de un
grupo familiar: minutos jugados en las dos ultimas semanas, minutos acumulados, portadas y una
vista general que combina los juegos de todos los integrantes para decidir que jugar.

La interfaz esta en espanol, con un diseno oscuro y compacto, dibujada a mano sobre GDI+ (salvo
los campos de texto y los desplegables, que son controles nativos oscurecidos). **No** usa
Electron, Chromium, WebView2, Node, Python, .NET ni Qt, y no necesita instalar nada para
ejecutarse: es un unico `.exe` que enlaza estaticamente el runtime de Visual C++.

---

## Que hace

- **Integrantes (A):** lista lateral con avatar, nombre publico, numero de juegos recientes y
  fecha de la ultima consulta. Permite anadir, editar y quitar integrantes.
- **Juegos recientes (B):** tarjetas compactas por juego con portada, nombre, tiempo de las
  ultimas dos semanas destacado y tiempo total acumulado. Boton para abrir la ficha en Steam.
- **Vista general (C):** agrega y elimina duplicados de todos los integrantes, mostrando los
  minutos de cada uno por titulo y resaltando de forma discreta el integrante con mas actividad
  en ese juego.
- **Filtros (D):** elegir integrante o ver a todos, ordenar por actividad reciente, buscar por
  nombre y ocultar juegos por debajo de un umbral de minutos.

Ademas:

- Etiquetas de actividad **Alta / Moderada / Baja** con umbrales configurables (explicados en
  Ajustes). La actividad **no** se presenta como "disponibilidad de copias" ni como "juego
  libre": son datos historicos, nunca estado en tiempo real.
- Actualizacion manual (integrante o todos), actualizacion automatica moderada opcional mientras
  la app esta abierta, indicador de la ultima actualizacion correcta y **cache local** de
  respuestas e imagenes.
- Si falla la red se muestran los datos en cache y se avisa de que pueden estar desactualizados.

## Que NO hace

- No muestra estado "en linea" ni "jugando ahora".
- No usa scraping, cookies ni endpoints no documentados.
- No pide la contrasena de Steam.
- No instala servicios ni procesos en segundo plano.

---

## Requisitos para compilar

- **Windows 10/11 x64.**
- **Visual Studio** con herramientas de C++ (se compilo con Visual Studio 18 Insiders, MSVC
  14.50, Windows SDK 10.0.26100). Sirve cualquier VS 2022 con el conjunto de herramientas C++
  y el SDK de Windows.
- **CMake >= 3.20** y **Ninja** (ambos se incluyen con Visual Studio).
- Sin dependencias de terceros: `nlohmann/json` va incluido como cabecera en
  `third_party/nlohmann/json.hpp`.

## Compilar y probar

La forma mas rapida, desde la raiz del repositorio:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Config Release -Test
```

- `-Config Debug` para una compilacion de depuracion.
- `-Test` compila y ejecuta la bateria de pruebas de consola.
- `-Clean` borra el directorio `build\<Config>` antes de compilar.

El script localiza Visual Studio con `vswhere`, llama a `vcvars64.bat`, configura CMake con el
generador Ninja y compila. Los binarios quedan en:

```
build\Release\SteamRecentPlay.exe   (aplicacion GUI)
build\Release\srp_tests.exe         (pruebas) 
```

### Compilacion manual (equivalente)

Desde un simbolo del sistema con el entorno de MSVC ya cargado
(`vcvars64.bat`) y con `cmake`/`ninja` disponibles:

```bat
cmake -S . -B build\Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build\Release
ctest --test-dir build\Release --output-on-failure
```

## Ejecutar

Doble clic sobre `build\Release\SteamRecentPlay.exe`, o desde la terminal:

```powershell
.\build\Release\SteamRecentPlay.exe
```

En el primer arranque, si no hay integrantes ni clave de API, se activa el **modo
demostracion** con datos ficticios claramente etiquetados (`DEMO - ...`). Sirve para ver la
interfaz sin credenciales.

---

## Uso

### 1. Clave de Steam Web API

1. Abre `steamcommunity.com/dev/apikey` (requiere iniciar sesion en Steam) y solicita una clave
   propia. Tambien puedes pulsar el boton **Clave API** de la app, que enlaza a esa pagina.
2. En la app, pulsa **Clave API**, pega la clave y pulsa **Guardar**. Con **Probar** se verifica
   contra Steam antes de usarla.

La clave se guarda **unicamente** en el Administrador de credenciales de Windows, con el
nombre de destino `SteamRecentPlay/SteamWebApiKey`. **Nunca** se escribe en `config.json`, en el
codigo fuente, en los registros ni se pasa por la linea de comandos. Puedes borrarla con el
boton **Eliminar** de ese mismo dialogo, o desde el Administrador de credenciales.

### 2. Integrantes

Pulsa **Anadir** y escribe:

- un **SteamID64** (17 digitos, empieza por `7656119...`);
- una **URL de perfil** que contenga `/profiles/<id>` o `/id/<nombre>`;
- o un **nombre de perfil** (se resuelve con `ResolveVanityURL`, y requiere clave de API).

Opcionalmente se puede asignar un **nombre local** para mostrarlo en la app.

### 3. Consultar actividad

- **Actualizar** refresca el integrante seleccionado.
- **Actualizar todo** refresca todos los integrantes.
- El boton **Vista general / Juegos recientes** alterna entre la lista detallada y la vista
  agregada de todos los integrantes.

### 4. Ajustes

- Actualizacion automatica mientras la app esta abierta y su intervalo (minutos).
- Mostrar u ocultar portadas.
- Umbrales de actividad reciente: minutos en las dos ultimas semanas a partir de los cuales la
  actividad se considera **Moderada** y **Alta** (por debajo de moderada es **Baja**).
- Ocultar juegos por debajo de un umbral de minutos recientes.
- Borrar la cache local (respuestas e imagenes).

### Estados de consulta

La app distingue explicitamente entre: consulta correcta con juegos; consulta correcta **sin**
juegos recientes; datos **no disponibles o restringidos** por privacidad; error de conexion;
clave invalida; y perfil invalido. Una lista vacia nunca se interpreta como prueba definitiva de
que no se ha jugado.

---

## Datos locales

Todo se guarda en `%LOCALAPPDATA%\SteamRecentPlay`:

```
config.json          integrantes y preferencias (nunca la clave de API)
cache\               respuestas de la API en cache
cache\images\        portadas descargadas
```

Las escrituras son atomicas (archivo temporal + reemplazo) y se agrupan para no castigar el
disco. La red y las descargas de imagenes se ejecutan en un unico hilo de trabajo, fuera del
hilo de la interfaz, con tiempos de espera, para que la ventana nunca se bloquee.

## Privacidad y red

- Solo se usan endpoints **documentados** de la API Web de Steam:
  `IPlayerService/GetRecentlyPlayedGames/v1`, `ISteamUser/GetPlayerSummaries/v2` y
  `ISteamUser/ResolveVanityURL/v1`, sobre HTTPS (WinHTTP).
- Las peticiones se hacen de forma espaciada y con cache, para ser respetuoso con los limites de
  uso (HTTP 429 se detecta y se muestra como "demasiadas solicitudes").
- No se accede a datos privados ni a cookies.

---

## Pruebas

`srp_tests` ejecuta **80 comprobaciones** sobre la logica no visual (`srp_core`), incluyendo:

- formateo de minutos y clasificacion de actividad reciente;
- identificacion, validacion y resolucion de perfiles;
- analisis de `GetRecentlyPlayedGames`, `GetPlayerSummaries` y `ResolveVanityURL`;
- perfiles privados o sin actividad;
- ordenacion por actividad reciente;
- cache de actividad y su refresco;
- errores de red;
- lectura y escritura de configuracion;
- datos del modo demostracion;
- construccion de URLs.

```powershell
.\build\Release\srp_tests.exe
```

---

## Limitaciones y lo que no se ha verificado

Compromiso de honestidad sobre el estado real del proyecto:

- **No se ha probado contra la API real de Steam**, porque en el entorno de desarrollo no habia
  una clave de API disponible. La integracion en vivo de `GetRecentlyPlayedGames`,
  `GetPlayerSummaries` y `ResolveVanityURL` (y el mapeo exacto de errores como clave invalida o
  perfil privado frente al servicio real) queda **sin verificar**. Lo que si esta verificado es
  el analisis de las respuestas JSON con datos de ejemplo en las pruebas, y la interfaz completa
  en modo demostracion.
- El modo demostracion usa **datos ficticios**; no representa a ninguna persona real.
- No representa ningun tipo de "disponibilidad de copias" ni estado en tiempo real.

Lo que **si** se ha verificado en este entorno:

- Compilacion limpia en Release, sin avisos en el codigo del proyecto.
- `SteamRecentPlay.exe` (~518 KiB) y `srp_tests.exe` (~349 KiB).
- Arranque de la aplicacion y estabilidad de la ventana principal.
- Interfaz completa revisada por capturas: vista "Juegos recientes", "Vista general", vista de un
  integrante, y los dialogos de Integrante, Ajustes, Clave API y Acerca de.
- Carga de portadas desde el CDN de Steam en modo demostracion.
- Las 80 comprobaciones de `srp_tests` pasan.

---

## Estructura del proyecto

```
CMakeLists.txt          definicion de objetivos (srp_core, SteamRecentPlay, srp_tests)
build.ps1               compilacion/pruebas por linea de comandos
resources\              manifiesto, icono, plantillas de dialogo (.rc) y resource.h
src\main.cpp            punto de entrada (wWinMain, GDI+, bucle de mensajes)
src\ui\                 interfaz: ventana, temas, dibujo, dialogos, hilo de trabajo, cache de imagenes
src\steam\              cliente HTTP (WinHTTP), API de Steam y datos de demostracion
src\storage\            configuracion en JSON, cache en disco y credenciales (Credential Manager)
src\common\             modelos compartidos, formateo y rutas/utilidades
tests\test_main.cpp     pruebas de consola de la logica no visual
third_party\nlohmann\   json.hpp (cabecera unica)
```

La logica no visual vive en `srp_core` y se puede probar sin abrir una ventana; la interfaz esta
separada en `src\ui`.

---

## Licencia

Proyecto de uso personal. `nlohmann/json` se distribuye bajo la licencia MIT (ver su propia
cabecera).
