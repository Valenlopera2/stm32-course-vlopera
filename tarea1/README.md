# Tarea 1 — Contador bidireccional con display de 7 segmentos

Proyecto bare-metal (registros CMSIS, sin HAL) para el **STM32F411RE** (ARM Cortex-M4).
Dos sensores incrementan o decrementan un contador de 16 bits que se muestra en un display
de 7 segmentos de 4 dígitos multiplexado.

- **Autor(a):** vlopera — valentinaloperau@gmail.com
- **Curso:** Taller 5 — Sistemas embebidos con STM32F4xx, UNAL 2026-2

---

## Funcionamiento

| Evento                                  | Acción                                             |
|-----------------------------------------|----------------------------------------------------|
| Flanco de bajada en **PC3** (sensor 1)  | `counter++` y cambia el LED de estado (PH1)        |
| Flanco de subida en **PA0** (sensor 2)  | `counter--` y cambia el LED de estado (PH1)        |
| Cada 2 ms (SysTick)                     | Refresca un dígito del display (ciclo de 8 ms)     |

- Antirrebote por software de **50 ms** en cada sensor (`DEBOUNCE_MS`).
- El contador es `uint16_t`: pasa de 65535 a 0 al subir y de 0 a 65535 al bajar.
  El display muestra solo los **4 últimos dígitos**.

### Máquina de estados

Las interrupciones solo *solicitan* un estado escribiendo en `fsm_Next_state`; el ciclo
principal lo consume y ejecuta la acción en `action_fsm()`.

```
                 EXTI3 (PC3)                 EXTI0 (PA0)
                     |                           |
                     v                           v
             +--------------+    +------+    +---------------+
             |   COUNT_UP   |<---| IDLE |--->|  COUNT_DOWN   |
             +--------------+    +------+    +---------------+
                     |            ^  |  ^            |
                     +------------+  |  +------------+
                                     | SysTick (cada 2 ms, solo si IDLE)
                                     v
                            +-----------------+
                            | REFRESH_DISPLAY |----> vuelve a IDLE
                            +-----------------+
```

---

## Conexiones

Display de 4 dígitos, **segmentos activos en bajo**; cada dígito se habilita con un
transistor **PNP** (base activa en bajo).

### Segmentos

| Segmento | Pin MCU | Pin del display |
|:--------:|:-------:|:---------------:|
| a        | PB12    | 11              |
| b        | PA12    | 7               |
| c        | PC11    | 4               |
| d        | PC10    | 2               |
| e        | PC12    | 1               |
| f        | PA11    | 10              |
| g        | PD2     | 5               |

### Dígitos (bases de los PNP)

| Dígito          | Pin MCU | Pin del display |
|-----------------|:-------:|:---------------:|
| 1 — miles       | PC5     | 12              |
| 2 — centenas    | PC6     | 9               |
| 3 — decenas     | PC2     | 8               |
| 4 — unidades    | PB7     | 6               |

### Entradas y LED

| Señal               | Pin MCU | Configuración                                   |
|---------------------|:-------:|-------------------------------------------------|
| Sensor 1 (sube)     | PC3     | Entrada, sin pull interno, pull-up externo 10 kΩ |
| Sensor 2 (baja)     | PA0     | Entrada, sin pull interno, pull-up externo 10 kΩ |
| LED de estado       | PH1     | Salida push-pull                                |

Todas las salidas son push-pull, baja velocidad, sin resistencias internas.

---

## Estructura del proyecto

```
tarea1/
├── Core/
│   └── Src/
│       ├── main.c                  # Lógica de la aplicación (GPIO, SysTick, EXTI, FSM)
│       ├── startup_stm32f411xx.S   # Código de arranque y tabla de vectores
│       ├── syscall.c / sysmem.c    # Soporte mínimo de newlib
├── cmake/                          # Toolchain, flags y lista de fuentes
├── CMakeLists.txt
├── CMakePresets.json               # Presets Debug / Release (Ninja)
├── stm32f411xe_flash.ld            # Linker script
└── .vscode/                        # Tareas y configuración de depuración (ST-LINK)
```

### Funciones principales (`main.c`)

| Función              | Descripción                                                  |
|----------------------|--------------------------------------------------------------|
| `init_gpio()`        | Habilita relojes de GPIOA/B/C/D/H y configura todos los pines |
| `init_timers()`      | SysTick a 1 ms con HSI de 16 MHz                             |
| `init_exti()`        | Asigna PA0→EXTI0 y PC3→EXTI3, flancos y prioridades del NVIC |
| `action_fsm()`       | Consume el estado pendiente y ejecuta su acción              |
| `update_display()`   | Apaga todos los dígitos, dibuja el número y enciende uno     |
| `draw_number()`      | Escribe los segmentos a–g usando `segment_table`             |
| `split_number()`     | Separa el contador en miles, centenas, decenas y unidades    |

---

## Compilar y cargar

### Requisitos

- VS Code con la extensión **STM32Cube for VS Code** (STM32 extension pack)
- `arm-none-eabi-gcc`, CMake ≥ 3.20 y Ninja
- Paquete **STM32Cube_FW_F4 V1.28.0** (headers CMSIS)
- Placa NUCLEO-F411RE (ST-LINK integrado)

> ⚠️ `CMakeLists.txt` define `CMSIS_ROOT` con una ruta absoluta
> (`/home/valentina/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.0`).
> Ajústala si compilas en otro equipo.

### Desde la terminal

```bash
cmake --preset Debug
cmake --build --preset Debug
```

El binario queda en `build/Debug/tarea1.elf`.

### Desde VS Code

1. Abre la carpeta `tarea1/`.
2. Selecciona el preset **Debug** y compila.
3. Inicia **STM32Cube: Launch ST-Link GDB Server** (F5). El proyecto se compila y se carga
   automáticamente; la ejecución se detiene en `main`.
4. Usa la vista **SFR** del depurador para inspeccionar `GPIOx->ODR`, `EXTI->PR`, etc.

---

## Notas

- La frecuencia del reloj se asume fija (HSI, 16 MHz). Si cambia, hay que recalcular
  `SYSTICK_RELOAD`.
- El refresco del display solo se agenda cuando la FSM está en `IDLE`, así que un evento de
  sensor tiene prioridad sobre un refresco.
