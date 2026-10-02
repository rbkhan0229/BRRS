/* Isolated diagnostic: CPU timestamps, no added SPI reads or RF-loop printing. */
#if BRRS_EXPERIMENT == 1 && BRRS_CODE_DIAGNOSTIC
#define BD_RING 512U
#define BD_ARCHIVE 8192U
#define BD_GAPS 256U
typedef struct {uint32_t tick,arg;uint16_t kind,line;} bd_event_t;
typedef struct {uint32_t cpu_before,cpu_after,rf_now;} bd_anchor_t;
typedef struct {uint32_t start,end,rf_start,rf_end,offset,count;uint16_t prev,next;bd_anchor_t before,after;} bd_gap_t;
static bd_event_t bd_ring[BD_RING],bd_archive[BD_ARCHIVE];
static bd_gap_t bd_gaps[BD_GAPS];
static uint32_t bd_n,bd_saved,bd_ngap,bd_truncated,bd_overflow,bd_rx_bad,bd_cfg_bad,bd_seq_bad;
static uint32_t bd_first,bd_last,bd_last_tick,bd_last_radio,bd_sync_count,bd_missing,bd_initial,bd_tail;
static bd_anchor_t bd_last_anchor;
static uint32_t bd_counts[12],bd_bits[8];
static void bd_event(uint16_t kind,uint32_t arg,uint16_t line) {
 if(!bd_last)return;
 bd_event_t *e=&bd_ring[bd_n%BD_RING];e->tick=dwt_timer_get_cycles();e->kind=kind;e->arg=arg;e->line=line;bd_n++;
 if(kind<12U)bd_counts[kind]++;
}
/* W1C=0 releases the DW3000 SYS_TIME read latch without clearing status bits.
 * Bracket the radio read with MCU cycles once per CRC-good beacon, outside
 * the RX-error/rearm critical path. */
static bd_anchor_t bd_sample_anchor(void) {
 bd_anchor_t a;
 dwt_writesysstatuslo(0U);
 a.cpu_before=dwt_timer_get_cycles();
 a.rf_now=dwt_readsystimestamphi32();
 a.cpu_after=dwt_timer_get_cycles();
 return a;
}
static void bd_gap(uint16_t next,uint32_t tick,uint32_t radio,bd_anchor_t anchor) {
 if(bd_ngap>=BD_GAPS){bd_overflow++;return;}
 uint32_t n=bd_n>BD_RING?BD_RING:bd_n;
 if(bd_n>BD_RING)bd_truncated++;
 if(bd_saved+n>BD_ARCHIVE){bd_overflow++;return;}
 bd_gap_t *g=&bd_gaps[bd_ngap++];g->prev=(uint16_t)bd_last;g->next=next;g->start=bd_last_tick;g->end=tick;g->rf_start=bd_last_radio;g->rf_end=radio;g->offset=bd_saved;g->count=n;g->before=bd_last_anchor;g->after=anchor;
 for(uint32_t i=bd_n-n;i<bd_n;i++)bd_archive[bd_saved++]=bd_ring[i%BD_RING];
}
static void bd_sync(uint16_t seq,uint32_t tick,uint32_t radio,bd_anchor_t anchor) {
 if(seq==0U||seq>TARGET_CYCLES||seq<=bd_last){bd_seq_bad++;return;}
 if(!bd_last){bd_first=seq;bd_initial=seq-1U;bd_missing=bd_initial;}
 else if(seq>bd_last+1U){bd_missing+=seq-bd_last-1U;bd_gap(seq,tick,radio,anchor);}
 bd_sync_count++;bd_last=seq;bd_last_tick=tick;bd_last_radio=radio;bd_last_anchor=anchor;bd_n=0U;
}
static void bd_poll(uint32_t status,bool sync) {
 if(!sync||!bd_last||!(status&(DWT_INT_RXFCG_BIT_MASK|SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR)))return;
 const uint32_t masks[8]={DWT_INT_RXFCG_BIT_MASK,SYS_STATUS_RXSTO_BIT_MASK,SYS_STATUS_RXPHE_BIT_MASK,SYS_STATUS_RXFCE_BIT_MASK,SYS_STATUS_RXFSL_BIT_MASK,SYS_STATUS_RXFTO_BIT_MASK,SYS_STATUS_RXPTO_BIT_MASK,SYS_STATUS_RXOVRR_BIT_MASK};
 for(unsigned i=0;i<8;i++)if(status&masks[i])bd_bits[i]++;
 bd_event(1U,status,__LINE__);
}
static int32_t bd_rxenable(int32_t mode,uint16_t line){
 bd_event(3U,(uint32_t)mode,line);int32_t rc=dwt_rxenable(mode);bd_event(4U,(uint32_t)rc,line);if(rc!=DWT_SUCCESS)bd_rx_bad++;return rc;
}
static void bd_forceoff(uint16_t line){bd_event(5U,0U,line);dwt_forcetrxoff();}
static int32_t bd_configure(dwt_config_t *cfg,uint16_t line){
 bd_event(6U,cfg==&config_sync?1U:0U,line);int32_t rc=dwt_configure(cfg);bd_event(7U,(uint32_t)rc,line);if(rc!=DWT_SUCCESS)bd_cfg_bad++;return rc;
}
static void bd_dump(void (*emit)(const char *)){
 char s[420];
 if(bd_last<TARGET_CYCLES){bd_tail=TARGET_CYCLES-bd_last;bd_missing+=bd_tail;if(bd_last)bd_gap(TARGET_CYCLES+1U,dwt_timer_get_cycles(),0U,(bd_anchor_t){0U,0U,0U});}
 snprintf(s,sizeof(s),"BRRS_BD_SUMMARY,version=1,first_seq=%lu,last_seq=%lu,sync_count=%lu,missing=%lu,initial_missing=%lu,tail_missing=%lu,gaps=%lu,events=%lu,truncated=%lu,overflow=%lu,rx_bad=%lu,cfg_bad=%lu,seq_bad=%lu,cpu_hz=64000000",(unsigned long)bd_first,(unsigned long)bd_last,(unsigned long)bd_sync_count,(unsigned long)bd_missing,(unsigned long)bd_initial,(unsigned long)bd_tail,(unsigned long)bd_ngap,(unsigned long)bd_saved,(unsigned long)bd_truncated,(unsigned long)bd_overflow,(unsigned long)bd_rx_bad,(unsigned long)bd_cfg_bad,(unsigned long)bd_seq_bad);emit(s);
 snprintf(s,sizeof(s),"BRRS_BD_BITS,rxgood=%lu,sfdto=%lu,phe=%lu,fce=%lu,fsl=%lu,fwto=%lu,pto=%lu,overrun=%lu",(unsigned long)bd_bits[0],(unsigned long)bd_bits[1],(unsigned long)bd_bits[2],(unsigned long)bd_bits[3],(unsigned long)bd_bits[4],(unsigned long)bd_bits[5],(unsigned long)bd_bits[6],(unsigned long)bd_bits[7]);emit(s);
 for(unsigned i=1;i<12;i++){snprintf(s,sizeof(s),"BRRS_BD_COUNT,kind=%u,n=%lu",i,(unsigned long)bd_counts[i]);emit(s);}
 for(uint32_t i=0;i<bd_ngap;i++){bd_gap_t *g=&bd_gaps[i];snprintf(s,sizeof(s),"BRRS_BD_GAP,id=%lu,prev=%u,next=%u,start=%lu,end=%lu,rf_start=%lu,rf_end=%lu,offset=%lu,count=%lu,anchor_before_cpu_before=%lu,anchor_before_cpu_after=%lu,anchor_before_rf=%lu,anchor_after_cpu_before=%lu,anchor_after_cpu_after=%lu,anchor_after_rf=%lu",(unsigned long)i,g->prev,g->next,(unsigned long)g->start,(unsigned long)g->end,(unsigned long)g->rf_start,(unsigned long)g->rf_end,(unsigned long)g->offset,(unsigned long)g->count,(unsigned long)g->before.cpu_before,(unsigned long)g->before.cpu_after,(unsigned long)g->before.rf_now,(unsigned long)g->after.cpu_before,(unsigned long)g->after.cpu_after,(unsigned long)g->after.rf_now);emit(s);}
 for(uint32_t i=0;i<bd_saved;i++){bd_event_t *e=&bd_archive[i];snprintf(s,sizeof(s),"BRRS_BD_EVENT,index=%lu,tick=%lu,kind=%u,arg=%lu,line=%u",(unsigned long)i,(unsigned long)e->tick,e->kind,(unsigned long)e->arg,e->line);emit(s);nrf_delay_ms(1U);}
 emit("BRRS_BD_END,version=2,clock_anchor=bracketed_sys_time_per_good_beacon");
}
#define dwt_rxenable(mode) bd_rxenable((mode),__LINE__)
#define dwt_forcetrxoff() bd_forceoff(__LINE__)
#define dwt_configure(cfg) bd_configure((cfg),__LINE__)
#else
#define bd_event(k,a,l) ((void)0)
#define bd_poll(s,c) ((void)0)
#define bd_sync(s,t,r,a) ((void)0)
#define bd_sample_anchor() ((void)0)
#define bd_dump(e) ((void)0)
#endif
