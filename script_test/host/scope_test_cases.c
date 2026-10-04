static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void reset_test(void)
{
    scope_sampler_stop();
    scope_usb_reset_apply();
    test_clock = test_previous = test_calls = test_last_dap = 0;
    test_fail_read = 0; test_stop_call = 0; test_cdc = 1; test_read_cost = 36;
}
int main(void)
{
    scope_var_t one = { 0x20001044U, 4, 4, 0 };
    uint32_t status_words[12];
    reset_test();
    assert(scope_sampler_configure(3, SCOPE_FLAG_DISCARD, 1, &one) == 0);
    assert(s_period_ticks == 72 && s_time_step == 3 && s_time_version == 1);
    assert(scope_start_now() == 0);
    test_clock = s_next_tick;
    scope_sampler_poll();
    assert(s_produced == 1 && s_t_time == 3);
    assert(s_pkt[s_fill_buf][2] == 0 || s_pkt[s_fill_buf][2] == 1);
    assert(scope_sampler_configure_ticks(60, SCOPE_FLAG_DISCARD, 1, &one) == 0);
    assert(!s_running && s_period_ticks == 60 && s_time_step == 60 && s_time_version == 2);
    assert(scope_start_now() == 0);
    uint8_t data_buf = s_fill_buf;
    for (unsigned i = 0; i < 124; i++) {
        test_clock = s_next_tick;
        scope_sampler_poll();
    }
    assert(s_produced == 124 && s_pkts == 2 && s_t_time == 7440);
    assert(s_pkt[data_buf][2] == 2 && get32(s_pkt[data_buf] + 8) == 0);
    for (unsigned i = 1; i < 124; i++)
        assert(get32(s_pkt[data_buf]+16+i*4) == get32(s_pkt[data_buf]+16+(i-1)*4)+1);
    scope_sampler_status(status_words, 12);
    assert(status_words[0] & (1U<<2));
    assert((status_words[11] & 0xffff) == 60 && (status_words[11] & (1U<<17)));
    assert(scope_sampler_configure_ticks(1, 0, 1, &one) == 0 && s_period_ticks == 48);
    assert(scope_sampler_configure_ticks(UINT32_MAX, 0, 1, &one) == 0 && s_period_ticks == 24000000);
    reset_test();
    assert(scope_sampler_configure_ticks(54, SCOPE_FLAG_DISCARD | SCOPE_FLAG_FAST_BATCH, 1, &one) == 0);
    assert(scope_start_now() == 0);
    test_clock = s_next_tick;
    scope_sampler_poll();
    assert(s_produced == 16 && s_t_time == 16 * 54 && s_dropped == 0);
    for (unsigned i = 0; i < 7; i++) { test_clock = s_next_tick; scope_sampler_poll(); }
    assert(s_produced == 124 && s_pkts == 2 && s_fill_n == 0);
    test_clock = s_next_tick;
    test_stop_call = test_calls + 3;
    scope_sampler_poll();
    assert(!s_running && s_produced == 127);
    reset_test();
    assert(scope_sampler_configure_ticks(54, SCOPE_FLAG_DISCARD | SCOPE_FLAG_FAST_BATCH, 1, &one) == 0);
    assert(scope_start_now() == 0);
    test_fail_read = 1; test_clock = s_next_tick;
    scope_sampler_poll();
    assert(s_swd_err == 1 && s_produced == 0 && s_pipe_dst == NULL);
    /* Timer rollover must not stall the bounded wait. */
    test_clock = s_next_tick = UINT32_MAX - 100;
    scope_sampler_poll();
    assert(s_produced == 16);
    /* A slow period always returns after one sample. */
    assert(scope_sampler_configure_ticks(96, SCOPE_FLAG_DISCARD | SCOPE_FLAG_FAST_BATCH, 1, &one) == 0);
    assert(scope_start_now() == 0); test_clock = s_next_tick; scope_sampler_poll();
    assert(s_produced == 1);
    /* Slow consumer: queued/in-flight packets are immutable even when all buffers fill. */
    reset_test();
    assert(scope_sampler_configure_ticks(54, SCOPE_FLAG_FAST_BATCH | SCOPE_FLAG_CDC_OFF, 1, &one) == 0);
    assert(scope_start_now() == 0 && !test_cdc);
    for (unsigned i = 0; i < 100 && s_fill_buf < SCOPE_TX_BUFS; i++) {
        test_clock = s_next_tick; scope_sampler_poll();
    }
    assert(s_fill_buf >= SCOPE_TX_BUFS && s_if_count == SCOPE_TX_BUFS && s_tx_active);
    uint8_t frozen[SCOPE_TX_BUFS][SCOPE_PACKET];
    memcpy(frozen, s_pkt, sizeof frozen);
    uint32_t produced_before = s_produced, reads_before = test_calls;
    for (unsigned i = 0; i < 3; i++) { test_clock = s_next_tick; scope_sampler_poll(); }
    assert(s_usb_drop == 3 && s_produced == produced_before && test_calls == reads_before);
    assert(memcmp(frozen, s_pkt, sizeof frozen) == 0 && s_pipe_dst == NULL);
    scope_sampler_tx_complete();
    assert(s_fill_buf < SCOPE_TX_BUFS);
    test_clock = s_next_tick; scope_sampler_poll();
    assert(s_produced == produced_before + 16);
    scope_sampler_stop(); assert(test_cdc);
    /* Recent DAP traffic still prevents entry into a batch. */
    reset_test();
    assert(scope_sampler_configure_ticks(54, SCOPE_FLAG_FAST_BATCH | SCOPE_FLAG_DISCARD, 1, &one) == 0);
    assert(scope_start_now() == 0);
    test_last_dap = s_next_tick - 1; test_clock = s_next_tick;
    scope_sampler_poll();
    assert(s_yield == 1 && s_produced == 0 && test_calls == 0);
    /* Multi-span plans never spin for 16 samples. */
    scope_var_t two[2] = { one, { 0x20002044U, 4, 4, 0 } };
    assert(scope_sampler_configure_ticks(54, SCOPE_FLAG_FAST_BATCH | SCOPE_FLAG_DISCARD, 2, two) == 0);
    assert(scope_start_now() == 0); test_last_dap = 0;
    test_clock = s_next_tick; scope_sampler_poll();
    assert(!s_pipe_ok && s_nspans == 2 && s_produced == 1);
    assert(scope_sampler_configure_ticks(96, SCOPE_FLAG_DISCARD, 1, &one) == 0);
    s_produced = 100000; s_dropped = 80000; s_usb_drop = 70000;
    s_swd_err = 90000; s_yield = 65536; test_clock = UINT32_MAX - 10;
    assert(scope_sampler_metrics(status_words, 11) == 0);
    assert(scope_sampler_metrics(status_words, 12) == 12);
    assert(status_words[0] == 0x31535348 && status_words[1] == UINT32_MAX - 10);
    assert(status_words[2] == 24000000 && status_words[3] == 100000);
    assert(status_words[4] == 80000 && status_words[5] == 70000 && status_words[6] == 90000);
    assert(status_words[7] == 65536 && status_words[10] == 96);
    one.size = 255;
    assert(scope_sampler_configure_ticks(60, 0, 1, &one) == -6 && s_nvars == 0);
    puts("scope host tests: protocol units, pipeline, packet boundary, limits, bounded batching, stop/error, rollover, USB ownership/starvation, DAP yield, multi-span PASS");
    return 0;
}
