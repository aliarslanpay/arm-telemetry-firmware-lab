# Bounded UART protocol

Integer fields are little-endian. Each frame is `0x7e`, escaped body, `0x7e`. Within the body, `0x7e` and `0x7d` are encoded as `0x7d` followed by the original byte XOR `0x20`. Adjacent delimiters are harmless. Decoder state persists across arbitrary host write/read boundaries.

| Raw offset | Field | Size |
|---|---|---|
| 0 | Version, currently 1 | 1 byte |
| 1 | Type | 1 byte |
| 2 | Payload length, 0–32 | 2 bytes |
| 4 | Sequence | 4 bytes |
| 8 | Payload | 0–32 bytes |
| 8 + length | CRC-16/CCITT-FALSE | 2 bytes |

CRC uses polynomial `0x1021`, initial value `0xffff`, no reflection, no final XOR; covers version through payload, excluding delimiters/escaping and CRC itself. Check vector `123456789` gives `0x29b1`. Raw body limit is 42 bytes; worst-case wire allocation is 86 bytes. The encoder computes required capacity before writing. Unknown types with otherwise valid framing reach the application and receive an error ACK.

| Type | Direction | Payload |
|---|---|---|
| `0x01` Ping | Host → firmware | Empty |
| `0x02` Burst | Host → firmware | One byte, count 1–32 |
| `0x03` Stop | Host → firmware | Empty |
| `0x81` Sample | Firmware → host | Three uint32: timer tick, synthetic value, source (0=timer, 1=burst) |
| `0x82` ACK | Firmware → host | Two bytes: request type, result (0=OK, 1=bad command/payload) |
| `0x83` Status | Firmware → host | Eight uint32 counters below |
| `0x84` Resources | FreeRTOS → host | Four uint32 stack high-watermarks, unused words: acquisition, processing, output, idle |

ACK echoes the request sequence. Sample sequences count generation **attempts**, starting at 1; drops create visible gaps. Synthetic value is `(sequence * 17 + 23) % 1000`. Sample tick is a CMSDK timer count, nominal 100 ms per tick; burst samples share the current tick. They are not UTC timestamps or latency measurements.

Status counters in order: accepted wire frames; CRC errors; length errors; combined malformed/version/command errors; RX transport losses; sample generation attempts; newest-sample output drops; lost timer events. RX losses include software-dropped/discarded bytes, hardware overrun indications, and RTOS input-event bytes lost on flush. Timer losses include full four-tick buffer drops and discarded RTOS tick events. They do not estimate unseen losses suppressed by the emulator's input backpressure. Status/Resources sequence equals the Stop sequence, or 0 for automatic completion.

For Stop, ACK precedes Resources (RTOS only) and Status. Pending accepted output drains before exit. No normal ASCII output occurs after the initial `READY <target>` line. The scheduling experiment is a separate text-only image.

## Recovery and overflow policies

- Version and oversized declared length are rejected early; discard until the next delimiter. Counter increments once for that rejected frame.
- Bodies larger than 42 bytes are discarded without writing beyond the parser buffer. CRC/actual-length checks occur at the terminating delimiter.
- A delimiter ends a partial frame and starts a fresh frame. Dangling escapes/short headers increment malformed count. Fragmentation alone does not reset state.
- UART ISR handles one byte per entry; the 128-byte software ring drops newest bytes when full. On observed byte loss the consumer discards queued bytes and resets partial parser state. It resumes at a fresh delimiter, retaining counters.
- Bare-metal output FIFO and RTOS output queue each have eight slots. Samples use zero-wait/drop-newest; accepted order is preserved. Control responses make room by draining output (bare-metal) or waiting up to one second for queue capacity (RTOS, assert/fail on expiry).
- FreeRTOS input-event queue has 64 slots. If full, the acquisition task flushes it, counts discarded byte/tick events and inserts a Loss marker before subsequent data. This favors clean recovery over retaining an ambiguous partial frame.
- Status reports errors at bounded termination. Bad wire frames do not generate ACKs, avoiding an error-response flood. Well-framed invalid commands receive result 1 and keep the firmware running.
