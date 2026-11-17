/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Valentina Lopera Urrego
 * @brief          : Tarea #2 (Taller V, 2026-S2). Un encoder rotatorio y su botón
 *                   controlan la intensidad de un LED RGB y una salida de voltaje
 *                   constante, todo con PWM. Organizado como una Máquina de
 *                   Estados Finitos (FSM)
 ******************************************************************************
 *
 *** Mapeo de Pines:
 ** LED RGB (PWM a 20 kHz, una resistencia por color, ver LED_COMMON_ANODE):
 * - Rojo  -> PC8  (TIM3_CH3, AF02)
 * - Verde -> PB8  (TIM4_CH3, AF02)
 * - Azul  -> PB9  (TIM4_CH4, AF02)
 *
 ** SALIDA AL FILTRO (PWM a 20 kHz -> RC -> seguidor con op-amp MCP6002):
 * - PB1 (TIM3_CH4, AF02)
 *
 ** LECTURA DE FRECUENCIA (EXTI9, flanco de subida):
 * - PC9 -> cable desde PC8, para medir el periodo del PWM rojo
 *
 ** ENCODER (KY-040, enchufado directo al header):
 * - CLK -> PA4 (EXTI4, ambos flancos)
 * - DT  -> PA1 (EXTI1, ambos flancos)
 * - SW  -> PA0 (EXTI0, ambos flancos): el botón cambia el modo
 *
 ** LED DE ESTADO -> PH1 (cambia cada 250 ms con el SysTick, siempre)
 *
 *
 * ===================== DESCRIPCIÓN DEL SISTEMA ===================== *
 *
 * - El botón del encoder cambia de modo. Cada pulsación pasa al siguiente y
 *   después del 5 vuelve al 1. Antes de la primera pulsación todo está apagado.
 *
 *   Modo            Qué se ve                          Qué hace el giro del encoder
 *   --------------  ---------------------------------  ------------------------------------
 *   MODE_OFF        los tres colores al 25 % (inicial)  nada
 *   MODE_RED        rojo                               sube o baja el rojo (parte de 0)
 *   MODE_GREEN      rojo que quedó + verde             sube o baja el verde (parte de 0)
 *   MODE_BLUE       rojo y verde que quedaron + azul   sube o baja el azul (parte de 0)
 *   MODE_ALL        los tres como quedaron             sube o baja los tres al tiempo
 *   MODE_DC         LEDs apagados, salida PB1          sube o baja el voltaje constante
 *
 * - Al arrancar o al dar reset, el sistema queda en MODE_OFF con los tres colores al 25 %
 *   (DUTY_IDLE). Desde la primera pulsación los modos empiezan en 0 como se describe abajo.
 *
 * - Los colores se acumulan: al pasar de un modo al siguiente el color anterior se queda
 *   con la intensidad que tenía y el color nuevo empieza en 0. Así el rojo sigue visible
 *   aunque se baje el verde o el azul. En MODE_ALL el giro suma o resta a los tres colores
 *   (cada uno satura en 0 y en DUTY_MAX), y al llegar arriba el LED queda blanco.
 *
 * - En MODE_DC el PWM de PB1 pasa por el filtro RC (104 y 103) y el seguidor, y
 *   queda un voltaje casi plano: Vout = 3.3 V x duty. Fuera de ese modo la salida
 *   vale 0 V. Al entrar a MODE_DC parte de 0 V. Al pulsar de nuevo se vuelve a MODE_RED
 *   y los tres colores empiezan otra vez desde 0.
 *
 * - Giro del encoder: CLK y DT forman un código de cuadratura de 2 bits. Las ISR de EXTI4
 *   (CLK) y EXTI1 (DT) se disparan en cualquier flanco y llaman a encoder_Decode(), que
 *   compara el estado anterior con el nuevo en una tabla (enc_table): cada transición
 *   válida suma +1 o -1 a un acumulador y las inválidas (rebotes) suman 0. Cuando el
 *   acumulador llega a +4 o -4 (una muesca completa) se suma o resta un paso a enc_steps.
 *   Cada paso suma o resta DUTY_STEP al duty del modo activo, saturado entre 0 y DUTY_MAX.
 *
 * - TIM3 y TIM4 (16 bits) generan los PWM. Con PSC = 0 el reloj es de 16 MHz y con
 *   ARR = 799 el periodo es de 800 cuentas: 16 MHz / 800 = 20 kHz, con 800 niveles
 *   de duty (CCR = 0 es 0 % y CCR = 800 es 100 %). A 20 kHz no se ve parpadeo en
 *   los LEDs y el filtro deja un voltaje casi plano.
 *
 * - TIM2 (32 bits) corre libre a 16 MHz y sirve de cronómetro. En cada flanco de
 *   subida de PC9 (el PWM rojo) la ISR guarda su valor. Con FREQ_EDGES + 1 flancos
 *   hay FREQ_EDGES periodos completos y el promedio da la frecuencia en Hz
 *   (measured_freq_hz, se ve en el depurador). Debe dar unos 20000 Hz.
 *
 * - SysTick interrumpe cada 1 ms: lleva el tiempo (ticks) para el antirrebote del
 *   botón y del giro, y pide el cambio del LED PH1 cada 250 ms.
 *
 * - Reloj del sistema: HSI 16 MHz (sin PLL), los timers reciben 16 MHz.
 *
 *
 * ===================== FSM ===================== *
 *
 * Patrón Switch-Case. La FSM decide QUÉ pasa (política), las funciones driver hacen
 * el CÓMO (hardware). Las ISR solo levantan banderas volatile, get_next_event() las
 * convierte en un EVENTO y el while(1) nunca se bloquea.
 *
 * STATE_IDLE es el centro: es el único estado que lee eventos. Cada evento lo lleva
 * a un estado que hace UNA tarea y vuelve a IDLE.
 *
 *   Estado actual       Evento              Estado siguiente     Acción [ENTER]
 *   ------------------  ------------------  -------------------  ------------------------------------------
 *   STATE_IDLE          EV_BUTTON           STATE_CHANGE_MODE    -
 *   STATE_IDLE          EV_ENCODER_CHANGED  STATE_UPDATE_DUTY    -
 *   STATE_IDLE          EV_BLINK_TICK       STATE_BLINK          -
 *   STATE_IDLE          EV_FREQ_READY       STATE_MEASURE_FREQ   -
 *   STATE_CHANGE_MODE   (incondicional)     STATE_IDLE           mode_Next(): siguiente modo y pwm_Update()
 *   STATE_UPDATE_DUTY   (incondicional)     STATE_IDLE           duty_ApplySteps(encoder_TakeSteps())
 *   STATE_BLINK         (incondicional)     STATE_IDLE           led_Toggle()
 *   STATE_MEASURE_FREQ  (incondicional)     STATE_IDLE           freq_Calculate()
 *   STATE_ERROR         (cualquiera)        STATE_IDLE           recuperación
 *
 ******************************************************************************
 */

//Includes

#include "stm32f411xe.h"
#include <stdint.h>
#include <stm32f4xx.h>

//Constantes de configuración

#define PWM_PRESCALER 0 //Sin divisor: los timers de PWM cuentan a 16 MHz
#define PWM_RELOAD 799 //ARR: (799 + 1) cuentas a 16 MHz son 20 kHz
#define LED_COMMON_ANODE 0 //1 si la pata larga (común) del LED RGB va a 3.3 V: cada color se enciende con el pin en bajo. 0 si va a GND (cátodo común)
#define DUTY_MAX 800 //CCR para 100 % de duty (ARR + 1)
#define DUTY_STEP 50 //Cambio de duty por paso del encoder (50 de 800 = 6.25 %, llega al máximo en 16 pasos). Subirlo lo hace más rápido, bajarlo más fino
#define DUTY_IDLE 200 //Duty de los tres colores en MODE_OFF, al arrancar o tras un reset (200 de 800 = 25 %)
#define DUTY_INIT 0 //Duty con el que arranca cada modo al entrar a él (0 %)
#define ENCODER_DIR_SIGN 1 //Poner -1 si al girar a la derecha el duty baja
#define BUTTON_DEBOUNCE_MS 30 //Tiempo en ms que el botón debe estar sin cambios (sin flancos) antes de aceptar una pulsación nueva
#define BLINK_PERIOD_MS 250 //Periodo del LED de estado en ms
#define FREQ_EDGES 100 //Periodos del PWM rojo que se promedian en cada lectura
#define FREQ_TIMEOUT_CYCLES 16000 //Si pasa más de 1 ms (16000 ciclos) sin flancos, la ventana se reinicia
#define TIMER_CLOCK_HZ 16000000 //Frecuencia del reloj de los timers

//Estados de la FSM (el QUÉ)
typedef enum {
	STATE_IDLE, //Reposo: espera un evento sin hacer nada más (no bloquea)
	STATE_CHANGE_MODE, //Atiende el botón: pasa al siguiente modo
	STATE_UPDATE_DUTY, //Atiende el giro del encoder: suma o resta al duty del modo activo
	STATE_BLINK, //Atiende el tick del SysTick: cambia el estado del LED PH1
	STATE_MEASURE_FREQ, //Atiende la lectura del cronómetro: calcula la frecuencia del PWM rojo
	STATE_ERROR, //Estado inválido (trampa de hardware): se recupera hacia STATE_IDLE
	NUM_STATES //Centinela: cantidad de estados, sirve para validar el rango
} State_t;

//Modos del sistema (qué salida está activa y qué cambia el encoder)
typedef enum {
	MODE_OFF, //Estado inicial: los tres colores al 25 %, el giro no cambia nada
	MODE_RED, //Solo el rojo
	MODE_GREEN, //Solo el verde
	MODE_BLUE, //Solo el azul
	MODE_ALL, //Los tres colores con el mismo duty
	MODE_DC, //Voltaje constante en la salida del filtro (PB1)
	NUM_MODES //Centinela: cantidad de modos, sirve para dar la vuelta
} Mode_t;

//Eventos de la FSM (Según que cambian los estados)
typedef enum {
	EV_NONE, //No ha ocurrido nada
	EV_BUTTON, //Se pulsó el botón del encoder (PA0, flanco de bajada)
	EV_ENCODER_CHANGED, //El encoder avanzó o retrocedió al menos un paso (enc_steps distinto de 0)
	EV_BLINK_TICK, //Venció el periodo del blinky (250 ms)
	EV_FREQ_READY //El cronómetro midió FREQ_EDGES periodos del PWM rojo
} Event_t;

//Variables
State_t current_state = STATE_IDLE; //Estado actual de la FSM, siempre inicia en reposo
Mode_t mode = MODE_OFF; //Modo actual: una sola variable, por eso los modos son excluyentes
uint8_t state_entry_flag = 1; //Vale 1 al llegar a un estado para que su acción [ENTER] corra exactamente una vez

volatile uint8_t button_flag = 0; //Bandera que deja la ISR de PA0 (volatile: cambia fuera del flujo normal)
volatile uint8_t blink_flag = 0; //Bandera que deja la ISR del SysTick
volatile uint8_t freq_flag = 0; //Bandera que deja la ISR de PC9

volatile uint32_t ticks = 0; //Milisegundos desde el arranque (los cuenta el SysTick)
volatile uint32_t last_button_tick = 0; //Instante del último flanco del botón (válido o rebote), para el antirrebote
volatile int32_t enc_steps = 0; //Pasos del encoder pendientes (+ giro a la derecha, - a la izquierda), los suman las ISR de PA4 y PA1
volatile uint8_t enc_prev = 3; //Estado anterior del encoder: bit 1 = CLK, bit 0 = DT (en reposo los dos están en alto: 3)
volatile int8_t enc_accum = 0; //Transiciones válidas acumuladas de la muesca actual (4 = una muesca hacia la derecha)
const int8_t enc_table[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0}; //Cambio por transición, indexada con (estado anterior << 2) | estado nuevo. Las transiciones imposibles (rebotes) valen 0
volatile uint32_t freq_first = 0; //Valor de TIM2 en el primer flanco de la ventana
volatile uint32_t freq_last = 0; //Valor de TIM2 en el último flanco
volatile uint32_t freq_cycles = 0; //Ciclos de reloj que duraron FREQ_EDGES periodos
volatile uint16_t freq_edges = 0; //Flancos contados en la ventana actual
uint32_t measured_freq_hz = 0; //Frecuencia medida del PWM rojo en Hz (se ve en el depurador)

int32_t duty_value[NUM_MODES]; //Duty (CCR) de cada modo, entre 0 y DUTY_MAX. Con signo para comparar con los límites

//Cabecera de funciones

//Drivers de la FSM (el CÓMO)
Event_t get_next_event(void); //Lee (y limpia) las banderas de las ISR y la lectura del encoder, y las convierte en un evento
int32_t encoder_TakeSteps(void); //Devuelve los pasos acumulados (con signo) y los descuenta de enc_steps
void duty_ApplySteps(int32_t steps); //Suma steps x DUTY_STEP al duty del modo activo, con saturación
void mode_Next(void); //Pasa al siguiente modo (el color nuevo parte de 0 y los anteriores se conservan) y actualiza las salidas
void pwm_Update(void); //Escribe en los CCR el duty que le toca a cada salida según el modo
void encoder_Decode(void); //Lee CLK y DT, decodifica la transición con enc_table y acumula los pasos en enc_steps (la llaman las ISR)
void freq_Calculate(void); //Convierte los ciclos medidos con TIM2 en la frecuencia en Hz
void led_Toggle(void); //Cambia el estado del LED PH1

//Inicialización del hardware
void init_GPIO(void);
void init_EXTI(void);
void init_systick(void);
void init_timer3PWM(void);
void init_timer4PWM(void);
void init_timer2(void);

//Main

int main(void){
    Event_t current_event = EV_NONE; //Evento que se evalúa en cada pasada del ciclo infinito
    uint8_t i; //Índice para dar el duty inicial a cada modo

    for (i = 0; i < NUM_MODES; i++) {
        duty_value[i] = DUTY_INIT; //Cada modo arranca en 0 %
    }

    init_GPIO();
    init_EXTI();
    init_timer3PWM();
    init_timer4PWM();
    init_timer2();
    pwm_Update(); //Estado inicial: los tres colores al 25 % (MODE_OFF)
    init_systick(); //Último: las interrupciones del blinky empiezan con todo el hardware listo

    /* El ciclo infinito nunca termina: lee un evento, evalúa el estado y reacciona.
     * Regla de NO bloqueo: no hay delay(), cada pasada procesa a lo sumo UN evento y UNA transición. */
    while(1){

        //Safeguard: valida el estado antes de usarlo, si está fuera de rango se recupera con STATE_ERROR
        if (current_state >= NUM_STATES) {
            current_state = STATE_ERROR;
            state_entry_flag = 1;
        }

        switch (current_state) {

        case STATE_IDLE: //Estado de reposo
            //[ENTER] Aquí no hay nada que preparar
            if (state_entry_flag) {
                state_entry_flag = 0;
            }

            //[DO] Solo IDLE consume eventos: los otros estados duran una pasada y no lo necesitan
            current_event = get_next_event();

            //Transiciones
            if (current_event == EV_BUTTON) {
                current_state = STATE_CHANGE_MODE;
                state_entry_flag = 1;
            }
            else if (current_event == EV_ENCODER_CHANGED) {
                current_state = STATE_UPDATE_DUTY;
                state_entry_flag = 1;
            }
            else if (current_event == EV_BLINK_TICK) {
                current_state = STATE_BLINK;
                state_entry_flag = 1;
            }
            else if (current_event == EV_FREQ_READY) {
                current_state = STATE_MEASURE_FREQ;
                state_entry_flag = 1;
            }
            break;

        case STATE_CHANGE_MODE: //Atiende el botón
            //[ENTER] Pasa al siguiente modo, exactamente una vez por cada pulsación
            if (state_entry_flag) {
                mode_Next();
                state_entry_flag = 0;
            }
            //Transición incondicional: la tarea ya fue atendida, se regresa al reposo
            current_state = STATE_IDLE;
            state_entry_flag = 1;
            break;

        case STATE_UPDATE_DUTY: //Atiende el giro del encoder
            //[ENTER] Cambia el duty del modo activo, exactamente una vez por cada evento del encoder
            if (state_entry_flag) {
                duty_ApplySteps(encoder_TakeSteps());
                state_entry_flag = 0;
            }
            current_state = STATE_IDLE;
            state_entry_flag = 1;
            break;

        case STATE_BLINK: //Estado que atiende el tick del blinky
            //[ENTER] Cambia el LED, exactamente una vez por cada evento del blinky
            if (state_entry_flag) {
                led_Toggle();
                state_entry_flag = 0;
            }
            current_state = STATE_IDLE;
            state_entry_flag = 1;
            break;

        case STATE_MEASURE_FREQ: //Estado que atiende la lectura del cronómetro
            //[ENTER] Calcula la frecuencia, exactamente una vez por cada lectura lista
            if (state_entry_flag) {
                freq_Calculate();
                state_entry_flag = 0;
            }
            current_state = STATE_IDLE;
            state_entry_flag = 1;
            break;

        case STATE_ERROR: //STATE_ERROR comparte código con default porque la recuperación es la misma
        default: //Trampa: cualquier estado desconocido se recupera de forma segura hacia el reposo
            current_state = STATE_IDLE;
            state_entry_flag = 1;
            break;
        }
    }
    return 0;
}

/* Funciones driver de la FSM (el CÓMO) */

//Convierte las banderas de las ISR y el encoder en UN evento. Prioridad: botón > encoder > blinky > frecuencia
Event_t get_next_event(void){
    if (button_flag == 1) { //La ISR de PA0 dejó un evento pendiente
        button_flag = 0; //Se baja la bandera: el evento se atiende una sola vez
        return EV_BUTTON;
    }

    if (enc_steps != 0) { //Las ISR de PA4 y PA1 dejaron al menos un paso pendiente, a la derecha o a la izquierda
        return EV_ENCODER_CHANGED;
    }

    if (blink_flag == 1) { //La ISR del SysTick dejó un evento pendiente
        blink_flag = 0;
        return EV_BLINK_TICK;
    }

    if (freq_flag == 1) { //La ISR de PC9 dejó una lectura lista
        freq_flag = 0;
        return EV_FREQ_READY;
    }

    return EV_NONE;
}

//Devuelve los pasos acumulados (+ CW, - CCW) y los descuenta de enc_steps
int32_t encoder_TakeSteps(void){
    int32_t steps; //Copia de los pasos pendientes

    __disable_irq(); //Las ISR del encoder podrían sumar un paso entre leer y reiniciar
    steps = enc_steps;
    enc_steps = 0; //Se reinicia el acumulador
    __enable_irq();

    return steps * ENCODER_DIR_SIGN; //Con ENCODER_DIR_SIGN = -1 se invierte el sentido del giro
}

//Suma los pasos al duty del modo activo y lo limita entre 0 y DUTY_MAX. En MODE_ALL los suma a los tres colores
void duty_ApplySteps(int32_t steps){
    uint8_t i; //Índice del color (MODE_RED, MODE_GREEN y MODE_BLUE son consecutivos)

    if (mode == MODE_OFF) { //Sin modo activo el giro no cambia nada (los pasos ya se descontaron de enc_steps)
        return;
    }

    if (mode == MODE_ALL) { //Los tres colores suben o bajan al tiempo, cada uno desde donde haya quedado
        for (i = MODE_RED; i <= MODE_BLUE; i++) {
            duty_value[i] += steps * DUTY_STEP;
            if (duty_value[i] < 0) duty_value[i] = 0;
            if (duty_value[i] > DUTY_MAX) duty_value[i] = DUTY_MAX;
        }
    }
    else { //Un solo color (o la salida al filtro)
        duty_value[mode] += steps * DUTY_STEP; //Cambia el duty del modo activo (6.25 % por paso)
        if (duty_value[mode] < 0) duty_value[mode] = 0;
        if (duty_value[mode] > DUTY_MAX) duty_value[mode] = DUTY_MAX;
    }

    pwm_Update(); //Con OCxPE = 1 los CCR entran al terminar el periodo actual
}

//Pasa al siguiente modo. Después de MODE_DC vuelve a MODE_RED (MODE_OFF solo es el estado inicial)
void mode_Next(void){
    uint8_t i; //Índice del color

    mode = (Mode_t) (mode + 1);

    if (mode >= NUM_MODES) {
        mode = MODE_RED;
    }

    if (mode == MODE_RED) { //Empieza una vuelta nueva: los tres colores parten de 0
        for (i = MODE_RED; i <= MODE_BLUE; i++) {
            duty_value[i] = DUTY_INIT;
        }
    }
    else if ((mode == MODE_GREEN) || (mode == MODE_BLUE) || (mode == MODE_DC)) { //El color nuevo (o el voltaje) parte de 0
        duty_value[mode] = DUTY_INIT; //Los colores anteriores NO se tocan: conservan su intensidad
    }
    //En MODE_ALL no se reinicia nada: los tres colores siguen como estaban

    pwm_Update(); //Cambia qué salidas quedan encendidas
}

//Escribe el duty de cada salida según el modo. Los colores se acumulan; en MODE_DC los LEDs se apagan y solo funciona PB1
void pwm_Update(void){
    int32_t red = 0; //Duty del rojo
    int32_t green = 0; //Duty del verde
    int32_t blue = 0; //Duty del azul
    int32_t filter = 0; //Duty de la salida al filtro

    if (mode == MODE_OFF) {
        red = DUTY_IDLE; //Estado inicial: los tres colores al 25 %
        green = DUTY_IDLE;
        blue = DUTY_IDLE;
    }
    else if (mode == MODE_DC) {
        filter = duty_value[MODE_DC]; //Solo la salida al filtro
    }
    else { //Modos de color: cada color muestra el duty que tiene guardado
        red = duty_value[MODE_RED];
        green = duty_value[MODE_GREEN];
        blue = duty_value[MODE_BLUE];
    }

    TIM3->CCR3 = red; //Rojo, PC8
    TIM4->CCR3 = green; //Verde, PB8
    TIM4->CCR4 = blue; //Azul, PB9
    TIM3->CCR4 = filter; //Filtro, PB1
}

//Decodifica el encoder: compara el estado anterior de CLK y DT con el nuevo y acumula una muesca cada 4 transiciones válidas
void encoder_Decode(void){
    uint8_t state = 0; //Estado nuevo: bit 1 = CLK, bit 0 = DT

    if (GPIOA->IDR & GPIO_IDR_ID4) state |= 2; //CLK (PA4)
    if (GPIOA->IDR & GPIO_IDR_ID1) state |= 1; //DT (PA1)

    enc_accum += enc_table[(enc_prev << 2) | state]; //Suma +1, -1 o 0 según la transición
    enc_prev = state; //El estado nuevo pasa a ser el anterior

    if (enc_accum >= 4) { //Una muesca completa hacia la derecha
        enc_steps++;
        enc_accum = 0;
    }
    else if (enc_accum <= -4) { //Una muesca completa hacia la izquierda
        enc_steps--;
        enc_accum = 0;
    }
    else if (state == 3) { //De vuelta en reposo sin completar la muesca (rebote o giro a medias): se descarta
        enc_accum = 0;
    }
}

//Frecuencia = (ciclos por segundo x periodos medidos) / ciclos medidos
void freq_Calculate(void){
    uint32_t cycles; //Copia de los ciclos medidos

    __disable_irq(); //La ISR de PC9 podría cambiar freq_cycles a la mitad de la lectura
    cycles = freq_cycles;
    __enable_irq();

    if (cycles != 0) { //Evita dividir por cero
        measured_freq_hz = (TIMER_CLOCK_HZ * FREQ_EDGES) / cycles; //16 MHz x 100 / ciclos de 100 periodos
    }
}

//Cambia el estado del LED PH1
void led_Toggle(void){
    GPIOH->ODR ^= GPIO_ODR_OD1; //XOR con 1: invierte el bit sin tocar los otros pines
}

/* Funciones de inicialización del hardware */

void init_GPIO(void){

    /*
    PH1 (Blinky)
    */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOHEN;   //Se enciende la señal de reloj
    GPIOH->MODER &= ~GPIO_MODER_MODE1;   //Se limpian los bits
    GPIOH->MODER |= GPIO_MODER_MODE1_0; //Se configura en (01) salida de proposito general
    GPIOH->OTYPER &= ~GPIO_OTYPER_OT1; //Salida push-pull
    GPIOH->OSPEEDR &= ~GPIO_OSPEEDER_OSPEEDR1; //Velocidad baja
    GPIOH->PUPDR &= ~GPIO_PUPDR_PUPD1; //No pull up/down
    GPIOH->ODR &= ~GPIO_ODR_OD1; //Inicia apagado

    /*
    PWM del LED rojo (PC8 AF02 (TIM3 -> CH3)) y del filtro (PB1 AF02 (TIM3 -> CH4))
    */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN; //Señal de reloj puerto C
    GPIOC->MODER &= ~GPIO_MODER_MODE8; //Limpiamos (00)
    GPIOC->MODER |= GPIO_MODER_MODE8_1; //(10) Función alternativa
    GPIOC->AFR[1] &= ~GPIO_AFRH_AFRH0; //Limpiamos (PC8 está en el registro alto, posición 0)
    GPIOC->AFR[1] |= GPIO_AFRH_AFRH0_1; //(0010 AF02)

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN; //Señal de reloj puerto B
    GPIOB->MODER &= ~GPIO_MODER_MODE1; //Limpiamos (00)
    GPIOB->MODER |= GPIO_MODER_MODE1_1; //(10) Función alternativa
    GPIOB->AFR[0] &= ~GPIO_AFRL_AFRL1; //Limpiamos
    GPIOB->AFR[0] |= GPIO_AFRL_AFRL1_1; //(0010 AF02)

    /*
    PWM del LED verde (PB8 AF02 (TIM4 -> CH3)) y azul (PB9 AF02 (TIM4 -> CH4))
    */
    GPIOB->MODER &= ~(GPIO_MODER_MODE8 | GPIO_MODER_MODE9); //Limpiamos (00)
    GPIOB->MODER |= (GPIO_MODER_MODE8_1 | GPIO_MODER_MODE9_1); //(10) Función alternativa
    GPIOB->AFR[1] &= ~(GPIO_AFRH_AFRH0 | GPIO_AFRH_AFRH1); //Limpiamos (PB8 y PB9 están en el registro alto)
    GPIOB->AFR[1] |= (GPIO_AFRH_AFRH0_1 | GPIO_AFRH_AFRH1_1); //(0010 AF02 en ambos casos)

    /*
    EXTIs (Entradas)
    */

    //PC9 (Lectura de frecuencia: recibe el PWM rojo con un cable desde PC8)
    GPIOC->MODER &= ~GPIO_MODER_MODE9; //Limpiamos (00) modo entrada
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPD9; //Limpiamos
    GPIOC->PUPDR |= GPIO_PUPDR_PUPD9_1; //Pull-down: evita que flote si falta el cable

    //PA0 (Botón del encoder, SW)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN; //Señal de reloj puerto A
    GPIOA->MODER &= ~GPIO_MODER_MODE0; //Limpiamos (00) modo entrada
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD0; //Limpiamos
    GPIOA->PUPDR |= GPIO_PUPDR_PUPD0_0; //Pull-up: el botón lleva el pin a GND al pulsarlo

    //PA4 (Giro del encoder, CLK; interrumpe en ambos flancos)
    GPIOA->MODER &= ~GPIO_MODER_MODE4; //Limpiamos (00) modo entrada
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD4; //Limpiamos
    GPIOA->PUPDR |= GPIO_PUPDR_PUPD4_0; //Pull-up (el módulo también trae uno)

    /*
    Encoder (DT -> PA1, entrada con interrupción en ambos flancos)
    */
    GPIOA->MODER &= ~GPIO_MODER_MODE1; //Limpiamos (00) modo entrada
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD1; //Limpiamos
    GPIOA->PUPDR |= GPIO_PUPDR_PUPD1_0; //Pull-up (el módulo también trae uno)
}

void init_EXTI(void){
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN; //Reloj del SYSCFG, el multiplexor de EXTI vive ahí

    /*
    PA0 (Botón del encoder)
    */
    SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI0; //Limpiamos
    SYSCFG->EXTICR[0] |= SYSCFG_EXTICR1_EXTI0_PA; //La línea EXTI0 toma el pin 0 del puerto A
    EXTI->FTSR |= EXTI_FTSR_TR0; //Disparo por flanco de bajada (se pulsó)
    EXTI->RTSR |= EXTI_RTSR_TR0; //y por flanco de subida (se soltó): así también se ven los rebotes al soltar
    EXTI->IMR |= EXTI_IMR_MR0; //Desenmascara la línea 0
    NVIC_SetPriority(EXTI0_IRQn, 1); //Prioridad del botón
    __NVIC_EnableIRQ(EXTI0_IRQn); //Habilita la interrupción en el NVIC

    /*
    PA4 (Giro del encoder, CLK)
    */
    SYSCFG->EXTICR[1] &= ~SYSCFG_EXTICR2_EXTI4; //Limpiamos
    SYSCFG->EXTICR[1] |= SYSCFG_EXTICR2_EXTI4_PA; //La línea EXTI4 toma el pin 4 del puerto A
    EXTI->RTSR |= EXTI_RTSR_TR4; //Disparo por flanco de subida
    EXTI->FTSR |= EXTI_FTSR_TR4; //y por flanco de bajada: el decodificador necesita los dos
    EXTI->IMR |= EXTI_IMR_MR4; //Desenmascara la línea 4
    NVIC_SetPriority(EXTI4_IRQn, 1); //Prioridad del giro
    __NVIC_EnableIRQ(EXTI4_IRQn); //Habilita la interrupción en el NVIC

    /*
    PA1 (Giro del encoder, DT)
    */
    SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI1; //Limpiamos
    SYSCFG->EXTICR[0] |= SYSCFG_EXTICR1_EXTI1_PA; //La línea EXTI1 toma el pin 1 del puerto A
    EXTI->RTSR |= EXTI_RTSR_TR1; //Disparo por flanco de subida
    EXTI->FTSR |= EXTI_FTSR_TR1; //y por flanco de bajada
    EXTI->IMR |= EXTI_IMR_MR1; //Desenmascara la línea 1
    NVIC_SetPriority(EXTI1_IRQn, 1); //Prioridad del giro
    __NVIC_EnableIRQ(EXTI1_IRQn); //Habilita la interrupción en el NVIC

    //Estado inicial del encoder: se lee antes de que el primer giro dispare una interrupción
    enc_prev = 0;
    if (GPIOA->IDR & GPIO_IDR_ID4) enc_prev |= 2; //CLK
    if (GPIOA->IDR & GPIO_IDR_ID1) enc_prev |= 1; //DT

    /*
    PC9 (Lectura de frecuencia del PWM rojo)
    */
    SYSCFG->EXTICR[2] &= ~SYSCFG_EXTICR3_EXTI9; //Limpiamos
    SYSCFG->EXTICR[2] |= SYSCFG_EXTICR3_EXTI9_PC; //La línea EXTI9 toma el pin 9 del puerto C
    EXTI->RTSR |= EXTI_RTSR_TR9; //Disparo por flanco de subida (una vez por periodo)
    EXTI->IMR |= EXTI_IMR_MR9; //Desenmascara la línea 9
    NVIC_SetPriority(EXTI9_5_IRQn, 2); //Prioridad menor: interrumpe 20000 veces por segundo
    __NVIC_EnableIRQ(EXTI9_5_IRQn); //Habilita la interrupción en el NVIC
}

void init_systick(void){
    SysTick->LOAD = 16000-1; //16 MHz / 16000 = 1 kHz: interrumpe cada 1 ms
    SysTick->VAL = 0; //Reinicia el contador actual
    NVIC_SetPriority(SysTick_IRQn, 0); //Prioridad más alta: el tiempo no se debe perder
    SysTick->CTRL |= (SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk); //Reloj del procesador (16 MHz) e interrupción habilitada
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk; //Arranca el contador, siempre de último
}

void init_timer3PWM(void){
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN; //Reloj del TIM3

    TIM3->PSC = PWM_PRESCALER; //Sin divisor: tick de 62.5 ns (16 MHz)
    TIM3->ARR = PWM_RELOAD; //Periodo: (799 + 1) ticks = 50 us -> 20 kHz
    TIM3->CCR3 = 0; //Rojo apagado
    TIM3->CCR4 = 0; //Salida al filtro en 0 V
    TIM3->CR1 |= TIM_CR1_ARPE; //ARR con preload: el cambio de periodo no corta pulsos

    TIM3->CCMR2 |= (TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_2); //110: PWM modo 1 en el canal 3
    TIM3->CCMR2 |= TIM_CCMR2_OC3PE; //CCR3 con preload
    TIM3->CCMR2 |= (TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2); //110: PWM modo 1 en el canal 4
    TIM3->CCMR2 |= TIM_CCMR2_OC4PE; //CCR4 con preload
    TIM3->CCER |= (TIM_CCER_CC3E | TIM_CCER_CC4E); //Habilita la salida de los canales 3 y 4 al pin
    if (LED_COMMON_ANODE) {
        TIM3->CCER |= TIM_CCER_CC3P; //Rojo con polaridad invertida (CC3P): duty 0 deja el pin en alto y el LED apagado. El canal 4 (filtro) no se invierte
    }
    TIM3->EGR |= TIM_EGR_UG; //Fuerza una actualización para cargar PSC, ARR y los CCR antes de arrancar

    TIM3->CR1 |= TIM_CR1_CEN; //Arranca el contador, siempre de último
}

void init_timer4PWM(void){
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN; //Reloj del TIM4

    TIM4->PSC = PWM_PRESCALER; //Sin divisor: tick de 62.5 ns (16 MHz)
    TIM4->ARR = PWM_RELOAD; //Periodo: (799 + 1) ticks = 50 us -> 20 kHz
    TIM4->CCR3 = 0; //Verde apagado
    TIM4->CCR4 = 0; //Azul apagado
    TIM4->CR1 |= TIM_CR1_ARPE; //ARR con preload: el cambio de periodo no corta pulsos

    TIM4->CCMR2 |= (TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_2); //110: PWM modo 1 en el canal 3
    TIM4->CCMR2 |= TIM_CCMR2_OC3PE; //CCR3 con preload
    TIM4->CCMR2 |= (TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2); //110: PWM modo 1 en el canal 4
    TIM4->CCMR2 |= TIM_CCMR2_OC4PE; //CCR4 con preload
    TIM4->CCER |= (TIM_CCER_CC3E | TIM_CCER_CC4E); //Habilita la salida de los canales 3 y 4 al pin
    if (LED_COMMON_ANODE) {
        TIM4->CCER |= (TIM_CCER_CC3P | TIM_CCER_CC4P); //Verde y azul con polaridad invertida: duty 0 deja el pin en alto y el LED apagado
    }
    TIM4->EGR |= TIM_EGR_UG; //Fuerza una actualización para cargar PSC, ARR y los CCR antes de arrancar

    TIM4->CR1 |= TIM_CR1_CEN; //Arranca el contador, siempre de último
}

void init_timer2(void){
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN; //Reloj del TIM2

    TIM2->PSC = 0; //Sin divisor: cuenta a 16 MHz (1 cuenta = 62.5 ns)
    TIM2->ARR = 0xFFFFFFFF; //TIM2 es de 32 bit: no se desborda hasta los 268 s, y la resta de dos lecturas aguanta el desborde
    TIM2->EGR |= TIM_EGR_UG; //Fuerza una actualización para cargar PSC y ARR antes de arrancar

    TIM2->CR1 |= TIM_CR1_CEN; //Arranca el contador, sin interrupciones: solo se lee TIM2->CNT
}

/* Funciones ISR (solo registran el evento y regresan, sin lógica de la FSM) */

//Cada milisegundo cuenta el tiempo y cada 250 ms deja la bandera del blinky
void SysTick_Handler(void)
{
    ticks++; //Un ms más
    if ((ticks % BLINK_PERIOD_MS) == 0) { //Si pasó el periodo del blinky
        blink_flag = 1; //La FSM lo convierte en EV_BLINK_TICK
    }
}

//Botón del encoder en PA0 (EXTI0 tiene su propio vector)
void EXTI0_IRQHandler(void)
{
    if(EXTI->PR & EXTI_PR_PR0) {
        EXTI->PR = EXTI_PR_PR0; //Se baja escribiendo 1 (en EXTI->PR se limpia con 1, no con 0)

        /* Una pulsación es válida si el pin está en bajo y pasaron BUTTON_DEBOUNCE_MS sin ningún flanco.
         * last_button_tick se actualiza con CADA flanco (también con los rebotes al pulsar y al soltar),
         * así un rebote al soltar no se cuenta como una pulsación nueva. */
        if (((ticks - last_button_tick) >= BUTTON_DEBOUNCE_MS) && ((GPIOA->IDR & GPIO_IDR_ID0) == 0)) {
            button_flag = 1; //La FSM lo convierte en EV_BUTTON
        }
        last_button_tick = ticks; //Reinicia la ventana de antirrebote con cada flanco
    }
}

//Giro del encoder, CLK en PA4 (EXTI4 tiene su propio vector). Solo baja la bandera y decodifica el paso
void EXTI4_IRQHandler(void)
{
    if(EXTI->PR & EXTI_PR_PR4) {
        EXTI->PR = EXTI_PR_PR4; //Se baja escribiendo 1
        encoder_Decode(); //La FSM lo convierte en EV_ENCODER_CHANGED cuando enc_steps no es 0
    }
}

//Giro del encoder, DT en PA1 (EXTI1 tiene su propio vector)
void EXTI1_IRQHandler(void)
{
    if(EXTI->PR & EXTI_PR_PR1) {
        EXTI->PR = EXTI_PR_PR1; //Se baja escribiendo 1
        encoder_Decode();
    }
}

//Lectura de frecuencia en PC9 (EXTI9 comparte vector con las líneas 5 a 9)
void EXTI9_5_IRQHandler(void)
{
    uint32_t now; //Valor del cronómetro en este flanco

    if(EXTI->PR & EXTI_PR_PR9) {
        EXTI->PR = EXTI_PR_PR9; //Se baja escribiendo 1

        now = TIM2->CNT; //Lee el cronómetro de 32 bit
        if ((freq_edges != 0) && ((now - freq_last) > FREQ_TIMEOUT_CYCLES)) { //Pasó más de 1 ms sin flancos (el PWM se detuvo)
            freq_edges = 0; //Se descarta la ventana a medias
        }
        freq_last = now; //Guarda el último flanco
        if (freq_edges == 0) { //Primer flanco de la ventana
            freq_first = now; //Guarda el instante de inicio
        }
        freq_edges++; //Un flanco más
        if (freq_edges > FREQ_EDGES) { //Hay FREQ_EDGES periodos completos
            freq_cycles = now - freq_first; //Ciclos que duraron esos periodos
            freq_edges = 0; //Empieza una ventana nueva
            freq_flag = 1; //La FSM lo convierte en EV_FREQ_READY
        }
    }
}
