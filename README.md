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
- **Importacion automatica de integrantes:** con la clave de API configurada, los botones
  **Importar familia** e **Importar amigos** detectan solo tu cuenta de Steam en el equipo y
  traen los miembros de tu **grupo familiar de Steam** o de tu **lista de amigos**, con una
  lista para marcar a quien seguir. No hay que escribir el SteamID de cada persona.
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

Justo despues de guardar la clave por primera vez, la app intenta **importar tu grupo familiar
automaticamente** (ver el punto 2).

### 2. Integrantes (importacion automatica)

No hace falta escribir cada SteamID a mano. Hay dos botones en la barra lateral:

- **Importar familia** (principal): trae a los miembros de tu **grupo familiar de Steam**
  (Steam Families).
- **Importar amigos**: trae a tu **lista de amigos** de Steam.

En ambos casos:

1. Pulsa el boton correspondiente.
2. La app detecta **tu** cuenta de Steam leyendo la cuenta reciente del cliente de Steam
   instalado (`loginusers.vdf`). Si no puede, te pedira tu perfil una sola vez.
3. Obtiene los integrantes (nombre y avatar) y muestra una lista para marcar a quien quieres
   seguir. Usa **Ctrl/Mayus** para seleccionar varios, o los botones **Seleccionar todos** /
   **Ninguno**.
4. Pulsa **Anadir seleccionados**.

Si vuelves a pulsarlo mas adelante, solo apareceran los integrantes que aun no sigues.

#### De donde sale cada lista

- **Amigos:** de `ISteamUser/GetFriendList/v1` (requiere que tu lista de amigos sea **publica**
  en Steam; si es privada, Steam devuelve un error y la app te lo indica), y los nombres y
  avatares de `ISteamUser/GetPlayerSummaries/v2`.
- **Grupo familiar:** de tu propio cliente de Steam. El servicio `IFamilyGroupsService` de la
  Web API **no acepta una clave de API normal** (necesita un token de sesion de la web de
  Steam), asi que en su lugar la app lee la copia que Steam guarda en
  `Steam/userdata/<tu cuenta>/config/localconfig.vdf` (bloque `FamilyGroup`) y luego pide los
  nombres y avatares con `GetPlayerSummaries`. Es la misma fuente que usa el propio cliente; no
  hay scraping ni cookies, solo lectura de un archivo local. Si esa cuenta no pertenece a
  ningun grupo familiar, la app te lo indica.

> La clave de API de Steam **no** identifica por si sola a que cuenta pertenece, y no existe un
> endpoint publico documentado que lo haga. Por eso la app deduce tu cuenta del cliente local
> (o te la pide una vez) y la recuerda en tu `config.json`.

#### Alta manual (opcional)

Tambien puedes pulsar **Anadir** y escribir:

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
config.json          integrantes, preferencias y tu propio SteamID (nunca la clave de API)
cache\               respuestas de la API en cache
cache\images\        portadas descargadas
```

Las escrituras son atomicas (archivo temporal + reemplazo) y se agrupan para no castigar el
disco. La red y las descargas de imagenes se ejecutan en un unico hilo de trabajo, fuera del
hilo de la interfaz, con tiempos de espera, para que la ventana nunca se bloquee.

## Privacidad y red

- Solo se usan endpoints **documentados** de la API Web de Steam:
  `IPlayerService/GetRecentlyPlayedGames/v1`, `ISteamUser/GetPlayerSummaries/v2`,
  `ISteamUser/GetFriendList/v1` (para importar amigos) y `ISteamUser/ResolveVanityURL/v1`,
  sobre HTTPS (WinHTTP).
- La deteccion de tu propia cuenta lee unicamente `loginusers.vdf` del cliente de Steam
  instalado (un archivo local de Steam); no es scraping ni accede a la web de Steam.
- El **grupo familiar** se lee de `userdata/<cuenta>/config/localconfig.vdf` del cliente de
  Steam (bloque `FamilyGroup`), tambien un archivo local. El servicio `IFamilyGroupsService` de
  la Web API **no** se usa: exige un token de sesion de la web de Steam, no una clave de API.
- Las peticiones se hacen de forma espaciada y con cache, para ser respetuoso con los limites de
  uso (HTTP 429 se detecta y se muestra como "demasiadas solicitudes").
- No se accede a datos privados ni a cookies.

---

## Pruebas

`srp_tests` ejecuta **118 comprobaciones** sobre la logica no visual (`srp_core`), incluyendo:

- formateo de minutos y clasificacion de actividad reciente;
- identificacion, validacion y resolucion de perfiles;
- analisis de `GetRecentlyPlayedGames`, `GetPlayerSummaries` (individual y por lotes) y
  `ResolveVanityURL`;
- analisis de `GetFriendList` (incluida la deteccion de lista privada);
- lectura de `loginusers.vdf` del cliente de Steam (incluido el formato real con claves en
  minusculas);
- lectura del bloque `FamilyGroup` de `localconfig.vdf` y conversion de `accountid` a SteamID64;
- perfiles privados o sin actividad;
- ordenacion por actividad reciente;
- cache de actividad y su refresco;
- errores de red;
- lectura y escritura de configuracion (incluido tu perfil propio);
- datos del modo demostracion;
- construccion de URLs.

```powershell
.\build\Release\srp_tests.exe
```

---

## Limitaciones y lo que se ha verificado

Compromiso de honestidad sobre el estado real del proyecto.

Verificado con una **clave de API real** durante el desarrollo (en un equipo con Steam
instalado):

- `GetRecentlyPlayedGames` y `GetPlayerSummaries` devuelven actividad, nombres y avatares
  reales para varios integrantes.
- La **importacion automatica** funciona de extremo a extremo:
  - **Familia:** detecta la cuenta local, lee el grupo familiar de `localconfig.vdf` y carga los
    nombres de sus miembros con `GetPlayerSummaries` (probado con un grupo familiar real de 5
    miembros).
  - **Amigos:** lee `GetFriendList` y carga los nombres de los amigos con `GetPlayerSummaries`.
- Carga de portadas desde el CDN de Steam.

No verificado / limitaciones:

- La ruta de **lista de amigos privada** (error 401) y el mapeo exacto de otros errores (clave
  invalida, limite de peticiones 429) no se han ejercitado contra el servicio real; en las
  pruebas se cubren con datos de ejemplo.
- La deteccion de tu propia cuenta depende del cliente de Steam instalado; si falta, la app te
  pide tu perfil una sola vez.
- El **grupo familiar** depende de que Steam este instalado y haya descargado la copia local
  (`userdata/<cuenta>/config/localconfig.vdf`); si el cliente nunca ha abierto la seccion
  Familia, el bloque puede no existir todavia y la app lo indica.
- El modo demostracion usa **datos ficticios**; no representa a ninguna persona real.
- No representa ningun tipo de "disponibilidad de copias" ni estado en tiempo real.

Verificado en el entorno de desarrollo:

- Compilacion limpia en Release, sin avisos en el codigo del proyecto.
- `SteamRecentPlay.exe` (~568 KiB) y `srp_tests.exe`.
- Arranque de la aplicacion y estabilidad de la ventana principal.
- Interfaz completa revisada por capturas: vistas "Juegos recientes"/"Vista general"/integrante y
  los dialogos de Integrante, Ajustes, Clave API, Importar integrantes y Acerca de.
- Las 118 comprobaciones de `srp_tests` pasan.

---

## Estructura del proyecto

```
CMakeLists.txt          definicion de objetivos (srp_core, SteamRecentPlay, srp_tests)
build.ps1               compilacion/pruebas por linea de comandos
resources\              manifiesto, icono, plantillas de dialogo (.rc) y resource.h
src\main.cpp            punto de entrada (wWinMain, GDI+, bucle de mensajes)
src\ui\                 interfaz: ventana, temas, dibujo, dialogos, hilo de trabajo, cache de imagenes
src\steam\              cliente HTTP (WinHTTP), API de Steam, deteccion local (cuenta y grupo familiar) y datos demo
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
