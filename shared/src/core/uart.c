#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/gpio.h>

#include "core/uart.h"
#include "core/ring-buffer.h"

#define BAUD_RATE 115200
#define UART_BUFFER_SIZE 128 //for max of ~10msec between reads at 115200 baud 

/* Nucleo-F446RE: PA2 = USART2_TX, PA3 = USART2_RX
 * These are wired to the ST-LINK VCP internally (solder bridges SB13/SB14).
 * So you get a serial terminal over the same USB cable used for debugging. */
#define UART_PORT   (GPIOA)
#define UART_TX_PIN (GPIO2)
#define UART_RX_PIN (GPIO3)

/* Single-byte receive buffer — only holds the LAST received byte.
 *
 * SUGGESTION: This is a major limitation. If the host sends multiple bytes
 * before the main loop calls uart_read(), all but the last byte are lost.
 * Consider replacing with a ring buffer (circular buffer) of e.g. 64 or 128
 * bytes. That way the ISR enqueues bytes and the main loop dequeues at its
 * own pace without data loss. */

/* 'volatile' tells the compiler: "this variable can change at any time
 * (e.g. from an ISR), do NOT cache it in a register — always read from RAM."
 * Remove volatile and recompile at -O2 to see the busy-wait loop in
 * firmware.c hang forever. */
#if 0
static uint8_t data_buffer = 0;
static bool data_available = false;
#else
static ring_buffer_t rx_ring_buffer;
static uint8_t rx_buffer[UART_BUFFER_SIZE] = {0};
#endif 

/* USART2 Interrupt Service Routine
 * Called by the NVIC whenever USART2 has a pending event (RXNE or ORE).
 * The function name "usart2_isr" must match EXACTLY — libopencm3's vector
 * table uses weak symbols, and this definition overrides the default handler. */
void usart2_isr(void)
{
    /* ORE (Overrun Error): Set when a new byte arrives in the shift register
     * but the previous byte in the data register hasn't been read yet.
     * Reading the data register (usart_recv) clears both RXNE and ORE flags. */
    const bool overrun_ocured = usart_get_flag(USART2, USART_FLAG_ORE) == 1;

    /* RXNE (Read Data Register Not Empty): Set when a complete byte has been
     * shifted in and is ready to be read from the data register. */
    const bool data_received = usart_get_flag(USART2, USART_FLAG_RXNE) == 1;

    if (data_received || overrun_ocured)
    {
        /* usart_recv() reads the DR register, which clears both RXNE and ORE.
         * We MUST read DR even on overrun, otherwise the interrupt keeps firing. */
        #if 0
         data_buffer = (uint8_t)usart_recv(USART2);
        data_available = true;
        #else
        if(ring_buffer_write(&rx_ring_buffer,(uint8_t)usart_recv(USART2)) == false)
        {
            /* Buffer is full, byte is lost. */
        }
        #endif
    }

    /* SUGGESTION: Consider tracking overrun events separately (e.g. increment
     * a counter) so you can detect if bytes are being dropped during debugging.
     * Example:
     *     static volatile uint32_t uart_overrun_count = 0;
     *     if (overrun_ocured) uart_overrun_count++;
     */
}

void uart_setup(void)
{
    ring_buffer_setup(&rx_ring_buffer, rx_buffer,UART_BUFFER_SIZE);
    /* Step 1: Enable the USART2 peripheral clock.
     * The APB1 bus clock feeds USART2 on the F446RE.
     * GPIO clock for GPIOA must ALSO be enabled (assumed done elsewhere,
     * e.g. in system.c).
     *
     * SUGGESTION: It would be safer to also enable GPIOA clock here:
     *     rcc_periph_clock_enable(RCC_GPIOA);
     * to make uart_setup() self-contained and not depend on call ordering. */
    rcc_periph_clock_enable(RCC_USART2);

    /* Step 2: Configure PA2 and PA3 as Alternate Function (AF7 = USART2).
     * GPIO_MODE_AF: Pin is driven by the peripheral, not software.
     * GPIO_PUPD_NONE: No internal pull-up/pull-down (the ST-LINK VCP side
     * drives the line, so no pull resistors needed). */
    gpio_mode_setup(UART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, UART_TX_PIN | UART_RX_PIN);
    gpio_set_af(UART_PORT, GPIO_AF7, UART_TX_PIN | UART_RX_PIN);

    /* Step 3: Configure USART2 parameters.
     * 8N1 @ 115200 is the most common serial configuration:
     *   - 8 data bits
     *   - No parity (N)
     *   - 1 stop bit
     * This must match your PuTTY / terminal emulator settings exactly. */
    usart_set_mode(USART2, USART_MODE_TX_RX);

    /* Flow control: NONE means we don't use RTS/CTS hardware handshaking.
     * Fine at 115200 baud with short messages, but at high data rates or
     * continuous streaming, data loss is possible without flow control. */
    usart_set_flow_control(USART2, USART_FLOWCONTROL_NONE);
    usart_set_databits(USART2, 8);
    usart_set_baudrate(USART2, BAUD_RATE);
    usart_set_parity(USART2, USART_PARITY_NONE);
    usart_set_stopbits(USART2, USART_STOPBITS_1);

    /* Step 4: Enable RX interrupt and the USART peripheral.
     * nvic_enable_irq() tells the ARM NVIC to route USART2 interrupts to
     * the usart2_isr() handler.
     * usart_enable_rx_interrupt() sets the RXNEIE bit in USART_CR1 so that
     * the USART fires an interrupt whenever RXNE (or ORE) is set.
     *
     * SUGGESTION: You can set interrupt priority with nvic_set_priority()
     * before enabling. Default priority 0 (highest) may preempt other ISRs. */
    nvic_enable_irq(NVIC_USART2_IRQ);
    usart_enable_rx_interrupt(USART2);

    usart_enable(USART2);
}

/* Transmit a buffer of bytes, blocking until all are sent. */
void uart_write(uint8_t* data, const uint32_t length)
{
    /* SUGGESTION: parameter should be 'const uint8_t* data' since we don't
     * modify the buffer. This also lets callers pass const strings without
     * casting. */
    for (uint32_t i = 0; i < length; i++)
    {
        uart_write_byte(data[i]);
    }
}

/* Transmit a single byte. Blocks until the TX data register is empty. */
void uart_write_byte(uint8_t data)
{
    usart_send_blocking(USART2, (uint16_t)data);
}

/* Read up to 1 byte from the receive buffer.
 * Returns the number of bytes actually read (0 or 1).
 *
 * NOTE: Despite accepting a 'length' parameter, this function can only ever
 * return 0 or 1 byte because the underlying buffer is a single byte.
 * The 'length' param is only used as a "do you want to read?" guard.
 *
 * SUGGESTION: With a ring buffer, this could actually fill up to 'length'
 * bytes into the caller's data array, making the API genuinely useful for
 * reading multi-byte messages. */
uint32_t uart_read(uint8_t* data, const uint32_t length)
{
    #if 0
    if (length > 0 && data_available)
    {
        *data = data_buffer;
        data_available = false;
        return 1;
    }
    return 0;
    #endif 
    if(length == 0)
    {
        return 0;
    }
    for(uint32_t byte_read=0; byte_read < length; byte_read++)
    {
        if(ring_buffer_read(&rx_ring_buffer, &data[byte_read]) == false)
        {
            /* Buffer is empty, no more bytes to read. */
            return byte_read;
        }
    }
    return length;

}

/* Read the last received byte directly (non-blocking).
 * Caller should check uart_data_available() first. */
uint8_t uart_read_byte(void)
{
    /* SUGGESTION: This API is not very useful since it can only return the
     * last received byte, and if multiple bytes arrive before the main loop
     * reads them, all but the last are lost. With a ring buffer, this could
     * be implemented to read one byte from the buffer without removing it,
     * allowing the caller to peek at the next byte without consuming it. */
    #if 0
    data_available = false;
    return data_buffer;
    #endif
    uint8_t byte = 0;
    (void) uart_read(&byte, 1);
    return byte;
}

/* Returns true if the ISR has received a byte that hasn't been consumed yet. */
bool uart_data_available(void)
{
    return !ring_buffer_is_empty(&rx_ring_buffer);
}


