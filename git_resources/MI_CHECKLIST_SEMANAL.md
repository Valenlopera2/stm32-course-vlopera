# Mi checklist para el cambio de semana

> Versión personal del `CHECKLIST_SEMANAL.md` del profe, ajustada a mi repo (`stm32-course-vlopera`) y a los errores que ya me pasaron.
> Guárdala como `git_resources/MI_CHECKLIST_SEMANAL.md` (con otro nombre que el del profe, para que sus actualizaciones nunca choquen con la mía).

---

## Cómo está organizado mi repo

| Cosa | Qué es |
|---|---|
| `origin` | **Mi** repo en GitHub. Aquí subo todo. |
| `upstream` | Repo **del profe** (`namontoy/stm32-embedded-course`). Solo descargo de aquí, nunca subo. |
| `week-XX` | Una rama por semana. Cada semana contiene **todo** lo de las anteriores + lo nuevo. |
| `main` | Mi rama "segura": solo recibe semanas terminadas y que compilan. Nunca programo directo en ella. |
| `tarea1/` | Mi proyecto. El código está en `tarea1/Core/Src/` (no en `Src/` como dice la guía del profe). |

**Regla de oro:** `git fetch` → inspeccionar → `git merge`. Nunca `git pull` desde upstream.

**Regla de la cadena:** la semana nueva **siempre** se fusiona con la semana **inmediatamente anterior** (week-06 con week-05, nunca con week-04). Si me salto una, el código de esa semana no llega.

---

## Paso 0 — Definir las semanas (lo único que cambio cada vez)

Copio esto en la terminal y cambio los números. Todos los comandos de abajo usan estas variables, así que después solo copio y pego:

```bash
ANT=week-06     # la semana que acabo de terminar
NUE=week-07     # la semana que voy a empezar
```

> Las variables se pierden si cierro la terminal. Si la cierro, las vuelvo a definir.

---

## Paso 1 — Verificar que no hay nada a medias

```bash
git status
```

- Si dice **"Tienes rutas no fusionadas"** o **"Unmerged paths"** → tengo un merge sin terminar. Lo termino (ver Paso 6) o lo cancelo con `git merge --abort`.
- Si dice **"nada para hacer commit, el árbol de trabajo está limpio"** → sigo.

---

## Paso 2 — Guardar y subir lo que hice en la semana anterior

```bash
git checkout $ANT
git add -A
git status          # REVISAR: no debe haber .bak, build/ ni basura en verde
git commit -m "$ANT: describe aquí lo que hice"
git push -u origin $ANT
```

`git add -A` agrega todo lo que no esté en el `.gitignore`. Por eso **siempre** miro el `git status` antes del commit.

**Verificar que el código sí quedó en la rama** (este es el error que me pasó en week-05):

```bash
git ls-tree -r $ANT --name-only | grep -E "\.(c|h|S|ld)$|CMakeLists"
```

Tienen que salir `tarea1/Core/Src/main.c`, el startup `.S`, el linker script `.ld` y el `CMakeLists.txt`. Si no salen, **no sigo** hasta arreglarlo.

> Si no hice cambios esa semana, no hago un commit diciendo "no hay cambios". Si `git status` está limpio, simplemente sigo.

---

## Paso 3 — Traer e inspeccionar la semana nueva del profe

```bash
git fetch upstream $NUE
git log upstream/$NUE --oneline -5
```

Ojo: es `upstream/$NUE` **con barra**. `git log upstream week-06` (con espacio) da el error *"argumento ambiguo 'upstream'"*.

---

## Paso 4 — Crear mi rama de la semana nueva

```bash
git checkout -b $NUE upstream/$NUE
```

---

## Paso 5 — Traer mi trabajo de la semana anterior

```bash
git merge $ANT --no-edit
```

- Si dice **"Already up to date"** → todo bien, salto al Paso 7.
- Si termina sin conflictos → salto al Paso 6.2 (igual hay que forzar la config de la IA).
- Si dice **CONFLICTO** → Paso 6 completo.

---

## Paso 6 — Resolver conflictos

### 6.1 Ver qué archivos están en conflicto

```bash
git diff --name-only --diff-filter=U
```

### 6.2 Archivos del profe → siempre gana la semana nueva

Esto aplica a `AGENTS.md`, `ai-config/` y `resources/`. Lo hago **siempre**, haya conflicto o no:

```bash
git checkout upstream/$NUE -- AGENTS.md ai-config/ resources/
```

### 6.3 Mi código (`tarea1/...`) → gana mi versión

Si algún archivo de `tarea1/` aparece en conflicto:

```bash
git checkout --theirs -- tarea1/ruta/del/archivo.c
```

(`--theirs` = la rama que estoy trayendo, o sea **mi** semana anterior. `--ours` = la rama donde estoy parada.)

### 6.4 `.gitignore` en conflicto → conservar las dos versiones

Lo abro en el editor, borro las marcas `<<<<<<<`, `=======` y `>>>>>>>`, y dejo **todas** las líneas de ambos lados.

### 6.5 Confirmar

```bash
git diff --name-only --diff-filter=U     # no debe imprimir nada
git add -A
git status                               # revisar
git commit -m "$NUE: integro código de $ANT, conservo config IA de $NUE"
```

Si `git commit` dice *"nada para hacer commit"*, no pasa nada: el merge ya estaba confirmado y la config ya coincidía.

---

## Paso 7 — Verificar antes de subir

```bash
git ls-tree -r $NUE --name-only | grep -E "\.(c|h|S|ld)$|CMakeLists"
```

Deben salir los mismos archivos de mi código que en el Paso 2. Después abro VS Code y **compilo**. Si no compila, lo arreglo antes de seguir.

---

## Paso 8 — Subir la semana nueva a MI repo

```bash
git push -u origin $NUE
git branch -vv
```

El `-u` es obligatorio la primera vez. Sin él, la rama queda rastreando **el repo del profe** (porque la creé desde `upstream/...`), y no me aparece en GitHub.

En `git branch -vv` debo ver `[origin/week-XX]` al lado de la rama, **no** `[upstream/week-XX]`.

---

## Paso 9 — Entorno e IA

Abro el proyecto, confirmo que compila, y verifico que la IA reconoce el nuevo `AGENTS.md` (`cat AGENTS.md` debe mencionar la semana nueva). Luego hago la autoevaluación inicial con la IA.

---

## Mi rama `main` (las cosas seguras)

`main` es mi copia "limpia": solo entra una semana cuando ya está **terminada, compila y la entregué**. Así, si un merge sale mal en una semana, siempre tengo un punto seguro al que volver.

**Reglas:**
1. Nunca programo directo en `main`.
2. `main` solo recibe merges **desde** las semanas (`week-XX → main`), nunca al revés.
3. Cada vez que promuevo una semana, le pongo una etiqueta (tag) para encontrarla fácil.

### Crearla (una sola vez)

```bash
git fetch origin
git branch -a | grep main
```

- Si sale `remotes/origin/main`:
  ```bash
  git checkout -b main origin/main
  ```
- Si **solo** sale `remotes/upstream/main`:
  ```bash
  git checkout -b main upstream/main
  git push -u origin main
  ```
- Si ya existe `main` local (sin `remotes/`):
  ```bash
  git checkout main
  git branch -u origin/main     # que rastree MI repo, no el del profe
  ```

Uso `checkout -b main origin/main` explícito porque `main` existe en `origin` y en `upstream`, y `git checkout main` a secas no sabe cuál escoger.

### Promover una semana terminada a `main`

```bash
SEM=week-05                      # la semana que ya entregué

git checkout main
git merge $SEM --no-edit
```

Si hay conflictos, en `main` siempre gana la semana:

```bash
git checkout --theirs -- $(git diff --name-only --diff-filter=U)
git add -A
git commit -m "main: promuevo $SEM (entregada y compilando)"
```

Etiquetar, subir y volver a trabajar:

```bash
git tag entrega-$SEM
git push origin main --tags
git checkout $NUE
```

### Volver a una versión segura

```bash
git tag                                   # ver todas las entregas guardadas
git checkout entrega-week-05 -- tarea1/   # recuperar mi código tal como lo entregué
```

---

## Puesta al día (hacer UNA sola vez, ahora)

Mi estado actual es este:

- **week-05** ya tiene el código de `tarea1`, pero también tiene archivos `.bak` que no deberían estar.
- **week-06** tiene el código de week-04, pero **no** lo de week-05.

### 1. Limpiar los `.bak` de week-05

```bash
git checkout week-05
printf '\n# Respaldos automáticos\n*.bak\n' >> .gitignore
git rm --cached $(git ls-files '*.bak')
git add .gitignore
git commit -m "week-05: ignoro y saco archivos .bak del repo"
git push -u origin week-05
```

(`git rm --cached` los saca del repo pero **no** los borra de mi computador.)

### 2. Llevar week-05 a week-06

```bash
git checkout week-06
git merge week-05 --no-edit
git checkout upstream/week-06 -- AGENTS.md ai-config/ resources/
```

Si `.gitignore` queda en conflicto, aplico el Paso 6.4. Después:

```bash
git add -A
git commit -m "week-06: integro week-05, conservo config IA de week-06"
git push -u origin week-06
```

Si el último commit dice *"nada para hacer commit"*, está bien.

### 3. Crear `main` y promover week-05

Sigo la sección **"Mi rama main"** de arriba con `SEM=week-05`.

### 4. Verificar todo

```bash
git branch -vv
```

Todas mis ramas `week-XX` y `main` deben decir `[origin/...]`.

---

## Errores que ya me pasaron (y qué significan)

| Mensaje | Qué pasa | Qué hago |
|---|---|---|
| `argumento ambiguo 'upstream'` | Escribí `upstream week-06` con espacio | Uso `upstream/week-06` |
| `No es posible hacer merge porque tienes archivos sin fusionar` | Hay un merge anterior sin terminar | Lo termino (Paso 6) o `git merge --abort` |
| `No es posible realizar un commit porque tienes archivos sin fusionar` | Quedó al menos un archivo en conflicto | `git diff --name-only --diff-filter=U` y lo resuelvo |
| `CONFLICTO (agregar/agregar)` | El mismo archivo se creó en las dos ramas | Si es del profe: Paso 6.2. Si es mío: Paso 6.3 |
| La rama no aparece en GitHub | Nunca hice push, o rastrea `upstream` | `git push -u origin <rama>` |
| El código no aparece en una semana | Salté la cadena (fusioné una semana más vieja) | `git checkout <semana>` y `git merge <semana-anterior>` |
| `matches multiple remote tracking branches` | La rama existe en `origin` y en `upstream` | `git checkout -b <rama> origin/<rama>` |

**Comando salvavidas:** si un merge se enreda y no he hecho commit, `git merge --abort` me devuelve a como estaba antes. Mi trabajo **nunca** se pierde mientras esté en el commit de alguna rama.

---

## Resumen rápido (cuando ya me lo sepa)

```bash
ANT=week-06; NUE=week-07
git status
git checkout $ANT && git add -A && git status
git commit -m "$ANT: ..." && git push -u origin $ANT
git fetch upstream $NUE && git log upstream/$NUE --oneline -5
git checkout -b $NUE upstream/$NUE
git merge $ANT --no-edit
git checkout upstream/$NUE -- AGENTS.md ai-config/ resources/
git add -A && git commit -m "$NUE: integro $ANT"
git ls-tree -r $NUE --name-only | grep -E "\.(c|h|S|ld)$|CMakeLists"
git push -u origin $NUE
```
