static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void reset_test(void)
{
    scope_sampler_stop();
    scope_usb_reset_apply();
    test_clock = test_previous = test_calls = test_last_dap = 0;
    test_fail_read = 0; test_cdc = 1; test_read_cost = 36;
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
    one.size = 255;
    assert(scope_sampler_configure_ticks(60, 0, 1, &one) == -6 && s_nvars == 0);
    puts("scope host tests: protocol units, pipeline, packet boundary, limits PASS");
    return 0;
}
