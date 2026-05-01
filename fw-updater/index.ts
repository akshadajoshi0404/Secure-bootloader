/**
 * Firmware Updater — Host-side tool
 *
 * This script runs on the host PC and communicates with the STM32 bootloader
 * over a UART serial connection.  It implements the same fixed-length packet
 * protocol used by the embedded bootloader so that firmware binary chunks can
 * be transferred reliably with CRC-8 error detection and automatic
 * retransmission on corruption.
 *
 * Packet wire format (18 bytes total):
 *   [ length (1 byte) | data (16 bytes) | crc8 (1 byte) ]
 *
 * Control packets (single-byte payloads, rest of data field padded with 0xFF):
 *   ACK  (0x15) — sent to confirm successful receipt of a data packet
 *   RETX (0x19) — sent to request retransmission of the previous packet
 */
import {SerialPort} from 'serialport';

// ---------------------------------------------------------------------------
// Packet protocol constants
// ---------------------------------------------------------------------------

/** Number of bytes in the length field at the start of every packet. */
const PACKET_LENGTH_BYTES   = 1;

/** Number of payload data bytes in every packet (fixed, padded with 0xFF). */
const PACKET_DATA_BYTES     = 16;

/** Number of bytes used for the trailing CRC field. */
const PACKET_CRC_BYTES      = 1;

/** Zero-based index of the CRC byte within a raw packet buffer. */
const PACKET_CRC_INDEX      = PACKET_LENGTH_BYTES + PACKET_DATA_BYTES;

/** Total wire length of one packet in bytes. */
const PACKET_LENGTH         = PACKET_LENGTH_BYTES + PACKET_DATA_BYTES + PACKET_CRC_BYTES;

/** First data byte value that identifies an ACK control packet. */
const PACKET_ACK_DATA0      = 0x15;

/** First data byte value that identifies a RETX (retransmit) control packet. */
const PACKET_RETX_DATA0     = 0x19;

// ---------------------------------------------------------------------------
// Serial port configuration
// ---------------------------------------------------------------------------

/** OS-level path of the USB-to-UART adapter connected to the target board. */
const serialPath            = "COM6";

/** Baud rate — must match the bootloader's UART initialisation. */
const baudRate              = 115200;

// ---------------------------------------------------------------------------
// CRC-8 (polynomial 0x07, initial value 0x00)
// ---------------------------------------------------------------------------

/**
 * Computes a CRC-8 checksum over the supplied byte sequence.
 *
 * Algorithm: CRC-8/SMBUS — polynomial 0x07, no reflection, initial value 0.
 * This matches the implementation compiled into the embedded bootloader so that
 * both sides independently agree on the integrity of each packet.
 *
 * @param data - Raw bytes to checksum (Buffer or plain number array).
 * @returns Single-byte CRC value in the range 0x00–0xFF.
 */
const crc8 = (data: Buffer | Array<number>) => {
  let crc = 0;

  for (const byte of data) {
    // XOR current CRC with the incoming byte
    crc = (crc ^ byte) & 0xff;
    // Process each of the 8 bits
    for (let i = 0; i < 8; i++) {
      if (crc & 0x80) {
        // MSB set: shift left and XOR with polynomial
        crc = ((crc << 1) ^ 0x07) & 0xff;
      } else {
        // MSB clear: plain shift
        crc = (crc << 1) & 0xff;
      }
    }
  }

  return crc;
};

/**
 * Resolves after `ms` milliseconds, yielding control to the Node.js event loop
 * so that the 'data' event handler on the serial port can run between polling
 * iterations inside `waitForPacket`.
 *
 * @param ms - Time to wait in milliseconds.
 */
const delay = (ms: number) => new Promise(r => setTimeout(r, ms));

// ---------------------------------------------------------------------------
// Packet class — serialisation / deserialisation
// ---------------------------------------------------------------------------

/**
 * Represents a single protocol packet exchanged with the bootloader.
 *
 * The wire format is always exactly PACKET_LENGTH bytes:
 *   [ length (1) | data (16) | crc (1) ]
 *
 * When constructing outbound packets the data field is right-padded with 0xFF
 * bytes to fill the fixed 16-byte payload region.  When parsing inbound raw
 * bytes, the CRC is supplied explicitly (read straight from the wire) so the
 * caller can compare it against the computed value to detect corruption.
 */
class Packet {
  /** Number of meaningful payload bytes (excludes padding). */
  length: number;

  /** Full 16-byte data field (meaningful bytes followed by 0xFF padding). */
  data: Buffer;

  /** CRC-8 checksum covering the length field and all 16 data bytes. */
  crc: number;

  /** Pre-built raw buffer for the RETX control packet (request retransmit). */
  static retx = new Packet(1, Buffer.from([PACKET_RETX_DATA0])).toBuffer();

  /** Pre-built raw buffer for the ACK control packet (acknowledge receipt). */
  static ack = new Packet(1, Buffer.from([PACKET_ACK_DATA0])).toBuffer();

  /**
   * @param length - Number of meaningful bytes in `data` (before padding).
   * @param data   - Payload bytes; will be padded with 0xFF to 16 bytes.
   * @param crc    - Optional CRC to use directly (supply when deserialising
   *                 a received packet so the raw wire value is preserved for
   *                 integrity checking).
   */
  constructor(length: number, data: Buffer, crc?: number) {
    this.length = length;
    this.data = data;

    // Pad the payload to the fixed PACKET_DATA_BYTES width with 0xFF
    const bytesToPad = PACKET_DATA_BYTES - this.data.length;
    const padding = Buffer.alloc(bytesToPad).fill(0xff);
    this.data = Buffer.concat([this.data, padding]);

    if (typeof crc === 'undefined') {
      // Outbound packet: compute CRC from the current length + data fields
      this.crc = this.computeCrc();
    } else {
      // Inbound packet: store the wire CRC so it can be validated by the caller
      this.crc = crc;
    }
  }

  /**
   * Computes the CRC-8 checksum over the length byte followed by all 16 data
   * bytes.  This mirrors the calculation performed on the embedded side.
   */
  computeCrc() {
    const allData = [this.length, ...this.data];
    return crc8(allData);
  }

  /**
   * Serialises the packet to an 18-byte Buffer ready to be written to the
   * serial port:  [ length | data (16 bytes) | crc ].
   */
  toBuffer() {
    return Buffer.concat([ Buffer.from([this.length]), this.data, Buffer.from([this.crc]) ]);
  }

  /**
   * Returns true if this is a single-byte control packet whose first data byte
   * matches `byte` and the remaining 15 data bytes are all 0xFF (padding).
   *
   * @param byte - Expected value of data[0].
   */
  isSingleBytePacket(byte: number) {
    if (this.length !== 1) return false;
    if (this.data[0] !== byte) return false;
    // All padding bytes must be 0xFF (canonical control packet format)
    for (let i = 1; i < PACKET_DATA_BYTES; i++) {
      if (this.data[i] !== 0xff) return false;
    }
    return true;
  }

  /** Returns true if this packet is an ACK control packet. */
  isAck() {
    return this.isSingleBytePacket(PACKET_ACK_DATA0);
  }

  /** Returns true if this packet is a RETX (retransmit request) control packet. */
  isRetx() {
    return this.isSingleBytePacket(PACKET_RETX_DATA0);
  }
}

// ---------------------------------------------------------------------------
// Serial port and state
// ---------------------------------------------------------------------------

/**
 * SerialPort instance that wraps the USB-to-UART adapter.
 * The port is opened automatically on construction.
 */
const uart = new SerialPort({ path: serialPath, baudRate });

/**
 * Queue of fully-received, CRC-verified data packets waiting to be consumed
 * by `waitForPacket`.  Control packets (ACK / RETX) are handled inline and
 * are never pushed into this queue.
 */
let packets: Packet[] = [];

/**
 * The raw buffer of the most recently transmitted packet.  Kept so that it can
 * be resent verbatim when the remote side responds with a RETX request.
 */
let lastPacket: Buffer = Packet.ack;

/**
 * Writes a raw packet buffer to the serial port and remembers it as the last
 * transmitted packet so it can be retransmitted on demand.
 *
 * @param packet - Pre-serialised 18-byte packet buffer to send.
 */
const writePacket = (packet: Buffer) => {
  uart.write(packet);
  lastPacket = packet;
};

// ---------------------------------------------------------------------------
// Receive buffer
// ---------------------------------------------------------------------------

/**
 * Accumulation buffer for raw bytes arriving from the serial port.
 * Because UART data may arrive in arbitrarily-sized chunks (depending on OS
 * buffering), incoming bytes are appended here until a complete packet
 * (PACKET_LENGTH bytes) has been received.
 */
let rxBuffer = Buffer.from([]);

/**
 * Removes and returns the first `n` bytes from `rxBuffer`, advancing the
 * buffer's read position.  Analogous to a queue dequeue of n bytes.
 *
 * @param n - Number of bytes to consume.
 * @returns Buffer containing the consumed bytes.
 */
const consumeFromBuffer = (n: number) => {
  const consumed = rxBuffer.slice(0, n);
  rxBuffer = rxBuffer.slice(n);
  return consumed;
}

// ---------------------------------------------------------------------------
// UART receive handler — packet state machine
// ---------------------------------------------------------------------------

/**
 * Fires every time the serial port emits a 'data' event (i.e. whenever one or
 * more bytes arrive from the bootloader).  All received bytes are appended to
 * `rxBuffer`; once a full packet's worth of bytes has accumulated the packet
 * is parsed and the appropriate action is taken:
 *
 *  1. CRC mismatch   → send RETX to ask the bootloader to resend.
 *  2. RETX received  → resend `lastPacket` unchanged.
 *  3. ACK received   → no action needed; the bootloader confirmed our last send.
 *  4. Data packet    → push onto `packets` queue and send ACK.
 */
uart.on('data', data => {
  console.log(`Received ${data.length} bytes through uart`);
  // Append new bytes to the accumulation buffer
  rxBuffer = Buffer.concat([rxBuffer, data]);

  // Only attempt to parse once we have at least one complete packet
  if (rxBuffer.length >= PACKET_LENGTH) {
    console.log(`Building a packet`);
    // Consume exactly PACKET_LENGTH bytes from the front of the buffer
    const raw = consumeFromBuffer(PACKET_LENGTH);
    // Deserialise: length byte, 16-byte data slice, and the wire CRC
    const packet = new Packet(raw[0], raw.slice(1, 1+PACKET_DATA_BYTES), raw[PACKET_CRC_INDEX]);
    const computedCrc = packet.computeCrc();

    // CRC check — if the wire CRC doesn't match, the packet is corrupted
    if (packet.crc !== computedCrc) {
      console.log(`CRC failed, computed 0x${computedCrc.toString(16)}, got 0x${packet.crc.toString(16)}`);
      writePacket(Packet.retx); // Ask the bootloader to resend its last packet
      return;
    }

    // RETX — the bootloader is requesting we resend our previous packet
    if (packet.isRetx()) {
      console.log(`Retransmitting last packet`);
      writePacket(lastPacket);
      return;
    }

    // ACK — the bootloader successfully received our last packet; nothing to do
    if (packet.isAck()) {
      console.log(`It was an ack, nothing to do`);
      return;
    }

    // Data packet — store it in the queue and acknowledge receipt
    console.log(`Storing packet and ack'ing`);
    packets.push(packet);
    writePacket(Packet.ack);
  }
});

// ---------------------------------------------------------------------------
// Packet awaiter
// ---------------------------------------------------------------------------

/**
 * Asynchronously waits until at least one data packet is available in the
 * `packets` queue, then dequeues and returns the oldest one (FIFO order).
 *
 * The 1 ms polling interval is short enough for responsive processing while
 * still giving the Node.js event loop time to run the serial 'data' handler
 * between polls.
 *
 * @returns The next data packet from the receive queue.
 */
const waitForPacket = async () => {
  while (packets.length < 1) {
    await delay(1); // Yield to let the 'data' event handler populate the queue
  }
  const packet = packets[0];
  packets = packets.slice(1); // Dequeue
  return packet;
}

// Log the pre-built ACK buffer at startup for diagnostic purposes
console.log(Packet.ack)

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------


console.log(Packet.retx);
/**
 * Main async entry point.  Using an async function allows the use of `await`
 * and structured loops throughout the firmware update sequence.
 *
 * Current behaviour (prototype / test):
 *  1. Wait for the first packet sent by the bootloader.
 *  2. Log the received packet.
 *  3. Send back a data packet with a deliberately corrupted CRC to exercise
 *     the retransmission path on the bootloader side.
 */
const main = async () => {
  console.log('Waiting for packet...');
  const packet = await waitForPacket();
  console.log(packet);

  // Build a test packet and corrupt its CRC to trigger a RETX response
  //const packetToSend = new Packet(4, Buffer.from([5, 6, 7, 8]));
  //packetToSend.crc++; // Intentionally corrupt CRC for testing
  //uart.write(packetToSend.toBuffer());
}

main();
