# Sample audio (PTR)

Status: **settled** (2026-10-09). The record: [the files proposal](../proposals/202610080050-fs.md),
section 6.

**PTR** (PT/FM Rice) codes 8-bit and 16-bit samples losslessly. A sample's audio is a record of its
own: kind 0x10, number its cartridge, sub its index there ([store.md](store.md#a-record)). Its entry in
the sampler cartridge's samples' table gives its length, rate and coding
([instruments.md](instruments.md#samples-and-fusion-slots)).

## The record

| Bytes | Field |
|---|---|
| 0 | coding: 1 PTR 8-bit, 2 PTR 16-bit |
| 1-3 | length in samples |
| 4- | the index: the offset of every 16th block from the payload's start (u32 each), then every block's coded length (u16 each) |
| then | the blocks |

- **Samples** are signed (two's complement), 8 or 16 bits.
- **A block** is 256 samples (the last one fewer), counted from the sample's first, and decodes on its
  own. It is one stream of bits, read from each byte's most significant bit, padded to a whole byte
  at its end:
  - a mode byte: a predictor order 0-3, or 7 for raw;
  - raw: the samples as they are (8 or 16 bits each);
  - else the first *order* samples as they are, then for each further sample the prediction's error
    e = x − (0 | x1 | 2x1 − x2 | 3x1 − 3x2 + x3), as u = 2e (e ≥ 0) or −2e − 1, in partitions of 64:
    a Rice parameter k (4 bits for 8-bit samples, 5 bits for 16-bit), then every u as (u >> k) zeros,
    a one, and its k low bits. The parameter's highest value (15, or 31) stores the partition's u
    plainly instead (12 bits each for 8-bit samples, 20 for 16-bit).
- **Playing from anywhere** (START, a loop's start): the index finds the block, decoded from its
  start: at most 255 samples more, once a jump.
- **The index** costs 2.25 B a block, against 4 for a plain offset each.

## Measured

- 8-bit: 41-67 % of their raw size for a module's samples, 33 % for 94 drum and instrument samples;
- 16-bit: 50 % for the same 94 (13-88 % each); whole recordings, with their quiet tails, 34 %;
- so a 16-bit sample takes 3 times the flash of the same sound at 8 bits;
- decoding on the FM-1: ~0.24 us a sample (8-bit).

## Choices

- **Fixed predictors are enough:** a linear predictor (FLAC's kind) would save ~7 % on 16-bit samples
  and under 2 % on 8-bit ones, for ~8 more multiply-adds a sample in the audio interrupt.
- **8 or 16 bits:** a converter keeps the sample's depth, except that audio whose low byte is always
  0 (an 8-bit sound in a 16-bit file) is stored as 8-bit: as 16-bit it would take 3 times the room.
- **Placement:** the user area, by the uploader ([blob.md](blob.md#seeding)), read in place as it plays
  ([store.md](store.md#where-things-go)).
- **Samples come only from the PC.** On the device the only changes to one are deleting it
  ([instruments.md](instruments.md#deleting)) and copying it with its cartridge (CLONE, MOVE).
