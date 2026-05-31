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
import { info } from 'console';
import { write } from 'fs';
import {SerialPort} from 'serialport';
import * as fs from "fs/promises";
import * as path from "path";
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


//Bootloader constat definations
const BOOTLOADER_SIZE                = 0x8000; /* Size of the bootloader in bytes, used to calculate the offset for reading the firmware image from disk, in a real application you would want to ensure that this matches the actual size of your bootloader and that it is correctly aligned with the memory layout of your target device */
const VECTOR_TABLE_SIZE              = (0x01B0); //(0x1AC);16byte alligned change the location  /* Size of the vector table in bytes, used to calculate the offset for the firmware info structure and the firmware image in flash memory, in a real application you would want to ensure that this matches the actual size of your vector table and that it is correctly aligned with the memory layout of your target device */
//const FWINFO_SIZE                    = (9*4); /* Size of the firmware info structure in bytes (9 x uint32_t: sentinel, device_id, firmware_version, length, reserved[4], CRC32) */   

//const FWINFO_VALIDATE_FROM           = (VECTOR_TABLE_SIZE + FWINFO_SIZE);
const FWINFO_DEVICE_ID_OFFSET        = (VECTOR_TABLE_SIZE + (1*4)); /* Offset from the start of the firmware info structure to where the device ID is stored in flash memory, used to calculate the absolute address of the device ID in flash memory, in a real application you would want to ensure that this offset correctly accounts for the size of your firmware info structure and any padding or alignment requirements */
//const FWINFO_VERSION_OFFSET          = (VECTOR_TABLE_SIZE + (2*4)); /* Offset from the start of the firmware info structure to where the firmware version is stored in flash memory, used to calculate the absolute address of the firmware version in flash memory, in a real application you would want to ensure that this offset correctly accounts for the size of your firmware info structure and any padding or alignment requirements */
const FWINFO_LENGTH_OFFSET           = (VECTOR_TABLE_SIZE + (3*4)); /* Offset from the start of the firmware info structure to where the firmware length is stored in flash memory, used to calculate the absolute address of the firmware length in flash memory, in a real application you would want to ensure that this offset correctly accounts for the size of your firmware info structure and any padding or alignment requirements */
const FWINFO_CRC32_OFFSET            = (VECTOR_TABLE_SIZE + (8*4)); /* CRC32 is the 9th field (0-indexed: 8) of firmware_info_t, at byte offset 32 from the start of the struct */


const BL_PACKET_SYNC_OBSERVED_DATA0         = (0x20);
const BL_PACKET_FW_UPDATE_REQ_DATA0         = (0x31); 
const BL_PACKET_FW_UPDATE_RES_DATA0         = (0x37);
const BL_PACKET_DEVICE_ID_REQ_DATA0         = (0x3C);
const BL_PACKET_DEVICE_ID_RES_DATA0         = (0x3F);
const BL_PACKET_FW_LENGTH_REQ_DATA0         = (0x42);
const BL_PACKET_FW_LENGTH_RES_DATA0         = (0x45);
const BL_PACKET_READY_FOR_DATA_DATA0        = (0x48);
const BL_PACKET_UPDATE_SUCCESSFUL_DATA0     = (0x54);
const BL_PACKET_NACK_DATA0                  = (0x59);


const DEVICE_ID  = (0x42);
const SYNC_SEQ = [0xc4, 0x55, 0x7e, 0x10];
const DEFAULT_TIMEOUT_MS = (5000); /* Default timeout for communication operations in milliseconds, can be adjusted as needed */


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

// ---------------------------------------------------------------------------
// CRC-32 (polynomial 0x04C11DB7, reflected 0xEDB88320, initial value 0xFFFFFFFF)
// ---------------------------------------------------------------------------

/**
 * Computes a CRC-32 checksum over the supplied byte sequence.
 *
 * Algorithm: CRC-32/ISO-HDLC (also known as plain CRC-32) — reflected
 * polynomial 0xEDB88320, initial value 0xFFFFFFFF, final XOR 0xFFFFFFFF.
 * This matches the STM32 hardware CRC peripheral configuration used by the
 * embedded bootloader to validate the firmware image.
 *
 * @param data - Raw bytes to checksum (Buffer or plain number array).
 * @returns Unsigned 32-bit CRC value.
 */
const crc32 = (data: Buffer , length: number) => {
  let byte;
  let crc = 0xFFFFFFFF;
  let mask;

  for (let i = 0; i < length; i++) {
    byte = data[i];
    crc = (crc ^ byte) >>> 0;
    for (let j = 0; j < 8; j++) {
      mask = (-(crc & 1)) >>> 0;
      crc = ((crc >>> 1) ^ (0xEDB88320 & mask)) >>> 0;
    }
  }
  return (~crc) >>> 0;
};

/**
 * Resolves after `ms` milliseconds, yielding control to the Node.js event loop
 * so that the 'data' event handler on the serial port can run between polling
 * iterations inside `waitForPacket`.
 *
 * @param ms - Time to wait in milliseconds.
 */
const delay = (ms: number) => new Promise(r => setTimeout(r, ms));


class Logger
{
  static info(message: string) {console.log(`INFO: ${message}`);}
  static success(message: string) {console.log(`SUCCESS: ${message}`);}
  static error(message: string) {console.error(`ERROR: ${message}`);}   
}

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

  static createSingleBytePacket(byte: number) {
    return new Packet(1, Buffer.from([byte]));
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
uart.on('error', (err) => { console.error(`[UART error ignored]: ${err.message}`); });

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
/**
 * LOGGING CHANGE: Verbose debug flag.
 * When true, enables low-level hex dumps of every UART receive event and
 * raw packet bytes on CRC errors.  Set to false for clean production output
 * that only shows high-level progress and error messages.
 */
const VERBOSE_DEBUG = true;

uart.on('data', data => {
  // Append new bytes to the accumulation buffer
  rxBuffer = Buffer.concat([rxBuffer, data]);

  // LOGGING CHANGE: Hex dump of every incoming UART chunk, showing buffer
  // accumulation state.  Invaluable for diagnosing byte-level framing issues
  // but extremely noisy during normal operation — gated by VERBOSE_DEBUG.
  if (VERBOSE_DEBUG) {
    console.log(`  [RX] +${data.length} bytes | rxBuffer [${rxBuffer.length}]: ${rxBuffer.toString('hex').match(/../g)?.join(' ')}`);
  }

  // Process every complete packet that has accumulated in the buffer.
  // Using 'while' handles back-to-back packets (e.g. ACK + READY_FOR_DATA)
  // that arrive in a single OS read.
  while (rxBuffer.length >= PACKET_LENGTH) {
    const raw = consumeFromBuffer(PACKET_LENGTH);
    const packet = new Packet(raw[0], raw.slice(1, 1+PACKET_DATA_BYTES), raw[PACKET_CRC_INDEX]);
    const computedCrc = packet.computeCrc();

    // ── CRC mismatch → request retransmission ──
    if (packet.crc !== computedCrc) {
      Logger.error(`CRC mismatch (wire=0x${packet.crc.toString(16)} computed=0x${computedCrc.toString(16)}) — sending RETX`);
      // LOGGING CHANGE: Dump the raw 18-byte packet that failed CRC check
      // so we can visually inspect where framing went wrong.
      if (VERBOSE_DEBUG) console.log(`  [RAW] ${raw.toString('hex').match(/../g)?.join(' ')}`);
      writePacket(Packet.retx);
      continue;
    }

    // ── RETX from bootloader → resend last packet ──
    if (packet.isRetx()) {
      Logger.info(`<< RETX — retransmitting last packet`);
      writePacket(lastPacket);
      continue;
    }

    // ── ACK from bootloader → our last send was received OK ──
    if (packet.isAck()) {
      // LOGGING CHANGE: ACK receipts are very frequent during data transfer;
      // only log them in verbose mode to avoid flooding the console.
      if (VERBOSE_DEBUG) console.log(`  [ACK]`);
      continue;
    }

    // ── NACK from bootloader → abort ──
    if (packet.isSingleBytePacket(BL_PACKET_NACK_DATA0)) {
      Logger.error(`Received NACK from bootloader — aborting.`);
      process.exit(1);
    }

    // ── Data/command packet → queue and acknowledge ──
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
const waitForPacket = async (timeout = DEFAULT_TIMEOUT_MS) => {
  let timeWaited = 0;
  while (packets.length < 1) {
    await delay(1); // Yield to let the 'data' event handler populate the queue
    timeWaited += 1;
    if (timeWaited >= timeout) {
      throw Error (`Timeout waiting for packet after ${timeout} ms`);
    }
  }
  const packet = packets.shift()!; // Dequeue
  return packet;
}

const waitForSingleBytePacket = async (byte: number, timeout = DEFAULT_TIMEOUT_MS) => {
  const packet = await waitForPacket(timeout)
  .then(packet=> {
    if (packet.length !== 1 || packet.data[0] !== byte) {
    const formattedPacket = [...packet.toBuffer()].map(b => b.toString(16)).join(' ');
    throw new Error(`Received unexpected packet while waiting for single-byte packet with data[0] = 0x${byte.toString(16)}: ${formattedPacket}`);
    }
  })
  .catch((e: Error) => {
    Logger.error(e.message);
    console.log(rxBuffer);
    console.log(packets);
    process.exit(1);
  })
};
// Log the pre-built ACK buffer at startup for diagnostic purposes
//console.log(Packet.ack)

const syncWithBootloader = async (timeout = DEFAULT_TIMEOUT_MS) => {
  let timeWaited = 0;
  while(true) {
    uart.write(Buffer.from(SYNC_SEQ));
    await delay(1000); // Wait a bit before sending the next sync sequence
    timeWaited += 1000;
    if(packets.length > 0) {
      const packet = packets.shift()!; // Dequeue
      if(packet.isSingleBytePacket(BL_PACKET_SYNC_OBSERVED_DATA0)) {
        Logger.success(`Received expected sync acknowledgment: ${packet.data[0].toString(16)}`);
        return; // Exit the loop if we receive the expected sync acknowledgment from the bootloader
      }
      Logger.error(`Received unexpected packet while waiting for sync: ${packet.data[0].toString(16)}`);
      process.exit(1); // Exit with an error code if we receive an unexpected packet
    }
    if (timeWaited >= timeout) {
      Logger.error(`Timeout waiting for sync acknowledgment`);
      process.exit(1); // Exit with an error code if we timeout
    }
  }
}


// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
//console.log(Packet.retx);
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

  //console.log('Waiting for packet...');
  //const packet = await waitForPacket();
  //console.log(packet);

  // Build a test packet and corrupt its CRC to trigger a RETX response
  //const packetToSend = new Packet(4, Buffer.from([5, 6, 7, 8]));
  //packetToSend.crc++; // Intentionally corrupt CRC for testing
  //uart.write(packetToSend.toBuffer());

  if(process.argv.length > 3) {
    Logger.error("Unexpected command line arguments : fw-updater <signed fimware image path>  ");
    process.exit(1);
  }
  
  const FirmwareFileName = process.argv[2];
  Logger.info("Reading the firmware Image binary from disk...");
  const fwImage = await fs.readFile(path.join(process.cwd(),FirmwareFileName))
  const fwLength = fwImage.length;
  Logger.success(`Firmware image read successfully, size after slicing off bootloader: ${fwLength} bytes`);

//  Logger.info("Injecting into firmware information section in flash memory...");
//  fwImage.writeUInt32LE(fwLength, FWINFO_LENGTH_OFFSET); // Write the sentinel value at the correct offset in the firmware image
//  fwImage.writeUInt32LE(0x00010000, FWINFO_VERSION_OFFSET); // Write the sentinel value at the correct offset in the firmware image
//  const crcValue = crc32(fwImage.slice(FWINFO_VALIDATE_FROM),fwLength-(VECTOR_TABLE_SIZE + FWINFO_SIZE)); // Compute the CRC32 over the firmware image starting from the validate_from offset
//  Logger.info(`Computed CRC32 value 0x${crcValue.toString(16)} for the firmware image, writing it to the firmware info section in flash memory...`);
//  fwImage.writeUInt32LE(crcValue, FWINFO_CRC32_OFFSET); // Write the computed CRC32 value to the correct offset in the firmware image

  Logger.info(`Starting firmware update process, waiting for synchronizing with bootloader...`);
  await syncWithBootloader(); // Wait for the bootloader to send its sync acknowledgment packet
  // LOGGING CHANGE (related): Flush rxBuffer after sync to prevent leftover
  // sync-phase bytes from corrupting the first framed packet parse.
  rxBuffer = Buffer.from([]);
  Logger.success(`Synchronized with bootloader, ready to proceed with firmware update!`);

  Logger.info(`Sending firmware update request packet...`);
  const fwUpdateReqPacket = Packet.createSingleBytePacket(BL_PACKET_FW_UPDATE_REQ_DATA0);
  writePacket(fwUpdateReqPacket.toBuffer());
  await waitForSingleBytePacket(BL_PACKET_FW_UPDATE_RES_DATA0); // Wait for the bootloader to acknowledge the firmware update request
  Logger.success(`Bootloader acknowledged firmware update request, ready to proceed with firmware update!`);

  Logger.info(`Waiting for device ID request from bootloader...`);
  await waitForSingleBytePacket(BL_PACKET_DEVICE_ID_REQ_DATA0); // Wait for the bootloader to send a device ID request packet
  Logger.info(`Received device ID request from bootloader...`);
  const deviceID = fwImage[FWINFO_DEVICE_ID_OFFSET]; // Read the device ID from the firmware image at the correct offset
  const  devIDPacket = new Packet(2, Buffer.from([BL_PACKET_DEVICE_ID_RES_DATA0, deviceID]));
  writePacket(devIDPacket.toBuffer());
  Logger.info(`Responding with device ID response packet with device ID 0x${DEVICE_ID.toString(16)}, waiting for firmware length request from bootloader...`);

  Logger.info(`Waiting for firmware length request from bootloader...`);
  await waitForSingleBytePacket(BL_PACKET_FW_LENGTH_REQ_DATA0); // Wait for the bootloader to send a firmware length request packet

  const fwLengthPacketBuffer = Buffer.alloc(5);
  fwLengthPacketBuffer[0] = BL_PACKET_FW_LENGTH_RES_DATA0;
  fwLengthPacketBuffer.writeUInt32LE(fwLength, 1); // Write the firmware length as a big-endian 32-bit integer starting at index 1
  const fwLengthPacket = new Packet(5, fwLengthPacketBuffer);
  writePacket(fwLengthPacket.toBuffer());
  Logger.info(`Responding with firmware length response packet with firmware length ${fwLength} bytes, waiting for ready for data packet from bootloader
     (waiting for main application to be erased 1sec)...`);

  await delay(1000); // Wait a bit to give the bootloader time to erase the main application area before we start sending data packets
  Logger.info(`Waiting for ready for data packet from bootloader...2sec`);
  await delay(1000); // Wait a bit to give the bootloader time to erase the main application area before we start sending data packets
  Logger.info(`Waiting for ready for data packet from bootloader...3sec`);
  await delay(1000); // Wait a bit to give the bootloader time to erase the main application area before we start sending data packets
  
  // ═══════════════════════════════════════════════════════════════════════
  // FIRMWARE DATA TRANSFER
  // ═══════════════════════════════════════════════════════════════════════
  Logger.info(`Starting data transfer: ${fwLength} bytes in ${Math.ceil(fwLength / PACKET_DATA_BYTES)} packets...`);
  console.log(`────────────────────────────────────────────────────────────────`);

  let byteWritten = 0;
  let packetCount = 0;
  const totalPackets = Math.ceil(fwLength / PACKET_DATA_BYTES);
  const startTime = Date.now();

  while(byteWritten < fwLength) {
    await waitForSingleBytePacket(BL_PACKET_READY_FOR_DATA_DATA0);
    const chunkSize = Math.min(PACKET_DATA_BYTES, fwLength - byteWritten);
    const dataChunk = fwImage.slice(byteWritten, byteWritten + chunkSize);
    const dataPacket = new Packet(chunkSize-1, dataChunk);
    writePacket(dataPacket.toBuffer());
    byteWritten += chunkSize;
    packetCount++;

    // LOGGING CHANGE: Replaced per-packet hex dump with a compact progress
    // indicator that overwrites itself on a single line using \r.  Updates
    // every 10 packets (or on the final packet) to keep console output clean
    // while still giving the user real-time transfer feedback.
    if (packetCount % 10 === 0 || byteWritten >= fwLength) {
      const percent = ((byteWritten / fwLength) * 100).toFixed(1);
      const elapsed = ((Date.now() - startTime) / 1000).toFixed(1);
      process.stdout.write(`\r  [${percent}%] ${byteWritten}/${fwLength} bytes | packet ${packetCount}/${totalPackets} | ${elapsed}s elapsed`);
    }
  }
  console.log(''); // newline after progress
  console.log(`────────────────────────────────────────────────────────────────`);
  Logger.info(`Data transfer complete. Waiting for bootloader validation result...`);

  // Wait for either UPDATE_SUCCESSFUL or NACK from bootloader
  const validationPacket = await waitForPacket(10000);
  if (validationPacket.isSingleBytePacket(BL_PACKET_UPDATE_SUCCESSFUL_DATA0)) {
    Logger.success(`Firmware update completed successfully! (signature validated)`);
  } else if (validationPacket.isSingleBytePacket(BL_PACKET_NACK_DATA0)) {
    Logger.error(`Firmware validation FAILED on device — image was rejected (signature/integrity check failed).`);
    process.exit(1);
  } else {
    const formattedPacket = [...validationPacket.toBuffer()].map(b => b.toString(16).padStart(2,'0')).join(' ');
    Logger.error(`Unexpected response after data transfer: ${formattedPacket}`);
    process.exit(1);
  }
}

main()
//.catch(e => {throw e})  
.finally(() => {uart.close(); // Close the serial port when the main function completes, whether it succeeded or threw an error
});
