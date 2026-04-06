## User & Feedback
- [User Profile](user_profile.md) — microelectronics enthusiast, Arduino GIGA, DAC audio project, writes blog posts
- [Feedback](feedback_style.md) — user prefers terse, no unnecessary optimizations, show diffs

## Project State
- [Project State](project_state.md) — current architecture, what's done, what's pending
- [Blog Context](blog_context.md) — full refactoring context doc lives at .claude/BLOG_DRAFT_CONTEXT.md
- [Pinout](pinout.md) — PA7=D5 (MOSI), PB3=SPI header left (SCK), D8/D9 latches

## Dead Ends (tested and failed/rejected)
- [SRAM D2](dead_end_sram.md) — D2 SRAM1 needs clock+MPU config, doesn't work out of box
- [HSEM RLR](dead_end_hsem_rlr.md) — direct register access doesn't fire interrupts
- [HSEM FastTake](dead_end_hsem_fasttake.md) — FastTake is required, can't skip it
- [OpenAMP](dead_end_openamp.md) — RPC.h conflicts with manual HSEM handlers
- [memcpy volatile](dead_end_memcpy_volatile.md) — memcpy ignores volatile semantics
- [HSEM one-shot](dead_end_hsem_oneshot.md) — notifications must be re-armed in callback
- [Linker sections](dead_end_linker_sections.md) — .RAM_D2 gives different addresses per core
- [DMA copies](dead_end_dma_copies.md) — worked but became unnecessary
- [ISB/DSB barriers](dead_end_isb.md) — ISB removed, DSB downgraded to DMB
- [Rejected opts](dead_end_rejected_opts.md) — batch activate, loop unroll, DMA — filtered correctly

## Architecture Challenges
- [Why dual-core?](arch_why_dual_core.md) — single core might handle both, ~27% M7 utilization estimated
- [Stop-and-wait ACK](arch_stop_and_wait_protocol.md) — round-trip latency wasted, pipelining possible
- [No error recovery](arch_no_error_recovery.md) — no checksums/sequence numbers, relies on USB CDC reliability
- [AD1860 vs I2S DAC](arch_ad1860_vs_i2s_dac.md) — 70% of firmware complexity exists because of AD1860's interface
- [Why not USB Audio?](arch_why_not_usb_audio_class.md) — USB Audio class would make it a real sound card
- [Double vs triple buffer](arch_double_vs_triple_buffer.md) — 2 regions has ~54ms slack, triple is possible in 64KB
- [Byte-by-byte payload](arch_parser_byte_by_byte.md) — parser could bulk-copy payload instead of per-byte switch
- [M4 can't read serial](arch_m4_cant_read_serial.md) — forced split, raw byte pipe alternative considered

## Potential Refactorings
- [M4 blocking loop](refactor_m4_blocking_loop.md) — spin-wait wastes cycles, prevents overlapping work
- [Duplicate M4 branches](refactor_duplicate_m4_branches.md) — region 0/1 code is copy-pasted
- [SPI enable/disable per sample](refactor_spi_per_sample_enable.md) — SPI6 enabled+disabled every sample, major overhead
- [RingBuffer unnecessary?](refactor_ringbuffer_unnecessary.md) — 32KB ring may not be needed with bulk reads
- [Payload direct byte write](refactor_payload_direct_write.md) — LE wire = LE memory, skip int16 assembly
- [Region acquire race](refactor_acquire_region_race.md) — looks racy but is actually safe, needs comment
- [Global g_dac_instance](refactor_global_dac_instance.md) — global pointer for ISR, could use mbed::callback
- [SPI6 shared bus bottleneck](refactor_spi6_shared_bus.md) — L/R sequential on same SPI, prescaler tuning
- [CLI silent no-stream](refactor_cli_no_streaming_error.md) — cli.py does nothing without -s, no error
- [Metadata format fragility](refactor_metadata_struct_pack.md) — manual byte offsets, no shared constants
- [Header before payload](refactor_header_written_before_payload.md) — header written before samples arrive, safe but defensive
- [Ticker never detaches](refactor_ticker_never_detaches.md) — ISR runs forever, sample rate change bug
- [CLI debug path divergence](refactor_cli_prealloc_optimization.md) — debug/non-debug use different ACK paths
