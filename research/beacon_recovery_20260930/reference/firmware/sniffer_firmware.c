/* Isolated passive CH5 timebase sniffer. Never transmits. */
#include "deca_probe_interface.h"
#include <deca_device_api.h>
#include <dw3000_deca_regs.h>
#include <deca_spi.h>
#include <example_selection.h>
#include <port.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "nrf.h"
#include "../brrs_beacon_protocol.h"

#if defined(TEST_INTERFERENCE_NONHOP)
extern int SEGGER_RTT_ConfigUpBuffer(unsigned, const char *, void *, unsigned, unsigned);
extern unsigned SEGGER_RTT_WriteString(unsigned, const char *);
#define SN_ARM_MAGIC 0x534E4131UL
#define SN_STOP_MAGIC 0x534E5354UL
#define SN_SCHEMA "passive-timebase-v1"
#define SN_MAX_AUX 28000U
#define SN_MAX_BEACONS 128U
#define SN_MAX_WINDOWS 32U
#define SN_CPU_CYCLES_US 64UL
#define SN_SCAN_INTERVAL_US 2000000UL
#define SN_SCAN_MAX_US 100000UL
#define SN_INITIAL_SCAN_MAX_US 30000000UL
#define SN_MAX_RUN_US 60000000UL
#define SN_HI_FAULT_MASK (SYS_STATUS_HI_SPIERR_BIT_MASK | SYS_STATUS_HI_SPI_UNF_BIT_MASK | SYS_STATUS_HI_SPI_OVF_BIT_MASK | SYS_STATUS_HI_CMD_ERR_BIT_MASK)

typedef struct __attribute__((packed)) {uint32_t rf; uint16_t seq;} sn_record_t;
_Static_assert(sizeof(sn_record_t)==6U,"sniffer record packing");
/* RAM symbols are read only after STOP and HALT; no per-frame RTT output. */
volatile uint32_t sn_host_arm=0U, sn_host_stop=0U;
volatile uint32_t sn_state=0U, sn_error=0U, sn_rf_off=0U;
volatile uint32_t sn_aux_count=0U, sn_beacon_count=0U, sn_window_count=0U;
volatile uint32_t sn_aux_bad=0U, sn_beacon_bad=0U, sn_rx_errors=0U;
volatile uint32_t sn_spi_errors=0U, sn_initial_anchor=0U, sn_last_anchor=0U;
volatile uint32_t sn_first_aux_seq=0U, sn_last_aux_seq=0U;
volatile sn_record_t sn_aux_records[SN_MAX_AUX];
volatile sn_record_t sn_beacon_records[SN_MAX_BEACONS];
volatile uint32_t sn_window_start[SN_MAX_WINDOWS], sn_window_end[SN_MAX_WINDOWS];
static char sn_rtt[4096];
static bool sn_probed=false;
static dwt_config_t sn_sync={5,DWT_PLEN_512,DWT_PAC8,10,10,1,DWT_BR_6M8,DWT_PHRMODE_STD,DWT_PHRRATE_STD,513,DWT_STS_MODE_OFF,DWT_STS_LEN_64,DWT_PDOA_M0};
static dwt_config_t sn_aux={5,DWT_PLEN_64,DWT_PAC8,9,9,1,DWT_BR_6M8,DWT_PHRMODE_STD,DWT_PHRRATE_STD,65,DWT_STS_MODE_OFF,DWT_STS_LEN_64,DWT_PDOA_M0};
static uint32_t sn_now(void){return DWT->CYCCNT;}
static uint32_t sn_elapsed(uint32_t start){return (uint32_t)(sn_now()-start)/SN_CPU_CYCLES_US;}
static void sn_log(const char *s){SEGGER_RTT_WriteString(1U,s);SEGGER_RTT_WriteString(1U,"\n");}
static bool sn_spi_bad(void){dw_spi_burst_stats_t x={0};port_dw_spi_burst_get_stats(&x);return x.transfer_error_count||x.state_error_count||x.direct_timeout_count;}
static int sn_end(uint32_t err){
 char s[256];
 if(sn_probed){dwt_forcetrxoff();sn_rf_off=1U;}
 if(sn_spi_bad()){sn_spi_errors++;if(!err)err=16U;}
 sn_error=err;sn_state=err?4U:3U;
 snprintf(s,sizeof(s),"SNIFFER_END schema=%s error=%lu aux=%lu beacon=%lu windows=%lu aux_bad=%lu beacon_bad=%lu rx_errors=%lu spi_errors=%lu rf_off=%lu",SN_SCHEMA,(unsigned long)sn_error,(unsigned long)sn_aux_count,(unsigned long)sn_beacon_count,(unsigned long)sn_window_count,(unsigned long)sn_aux_bad,(unsigned long)sn_beacon_bad,(unsigned long)sn_rx_errors,(unsigned long)sn_spi_errors,(unsigned long)sn_rf_off);
 sn_log(s);return err?-1:0;
}
static int sn_configure(dwt_config_t *cfg){
 dwt_forcetrxoff();dwt_writesysstatuslo(0xffffffffUL);
 if(dwt_configure(cfg)!=DWT_SUCCESS)return -1;
 dwt_setrxtimeout(0U);
 if(dwt_rxenable(DWT_START_RX_IMMEDIATE)!=DWT_SUCCESS)return -1;
 return sn_spi_bad()?-1:0;
}
static int sn_rearm(void){
 dwt_forcetrxoff();dwt_writesysstatuslo(0xffffffffUL);
 if(dwt_rxenable(DWT_START_RX_IMMEDIATE)!=DWT_SUCCESS)return -1;
 return sn_spi_bad()?-1:0;
}
int interference_nonhop(void){
 uint64_t arm_wait=0U,run_cycles=0U; uint32_t arm_last,run_last,next_scan=SN_SCAN_INTERVAL_US,scan_start=0U;
 bool sync_mode=true,first_anchor=false;
 uint8_t frame[BRRS_BEACON_PSDU_BYTES+4U];
  if(SEGGER_RTT_ConfigUpBuffer(1U,"SN_LOG",sn_rtt,sizeof(sn_rtt),0U)<0)return -1;
 CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
 port_set_dw_ic_spi_fastrate();reset_DWIC();Sleep(2);
 if(dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf)!=DWT_SUCCESS)return sn_end(11U);
 sn_probed=true;arm_last=sn_now();
 while(!dwt_checkidlerc()){
  if(sn_host_stop==SN_STOP_MAGIC)return sn_end(0U);
  if(sn_elapsed(arm_last)>100000U)return sn_end(12U);
 }
 if(dwt_initialise(DWT_DW_INIT)!=DWT_SUCCESS)return sn_end(13U);
 dwt_forcetrxoff();sn_rf_off=1U;
 if(dwt_configure(&sn_sync)!=DWT_SUCCESS)return sn_end(14U);
 if(sn_spi_bad())return sn_end(16U);
 dwt_writesysstatuslo(0xffffffffUL);
 sn_state=1U;
 sn_log("SNIFFER_READY schema=passive-timebase-v1 channel=5 beacon_m=512 beacon_code=10 aux_m=64 aux_code=9 tx=0 initial_anchor_wait_us=30000000");
 arm_last=sn_now();
 while(sn_host_arm!=SN_ARM_MAGIC){
  uint32_t n=sn_now();arm_wait+=(uint32_t)(n-arm_last);arm_last=n;
  if(sn_host_stop==SN_STOP_MAGIC)return sn_end(0U);
  if(arm_wait>=90000000ULL*SN_CPU_CYCLES_US)return sn_end(17U);
  nrf_delay_us(100);
 }
 if(sn_host_stop==SN_STOP_MAGIC)return sn_end(0U);
 run_last=sn_now();sn_state=2U;sn_rf_off=0U;
 if(sn_configure(&sn_sync)!=0)return sn_end(18U);
 scan_start=0U;sn_window_start[0]=dwt_readsystimestamphi32();
 sn_log("SNIFFER_START mode=passive");
 for(;;){
  uint32_t status,hi,elapsed,rf,tick;uint16_t len;
  if(sn_host_stop==SN_STOP_MAGIC)return sn_end(0U);
  /* CYCCNT wraps every ~67 ms at 64 MHz; accumulate each short delta. */
  tick=sn_now();run_cycles+=(uint32_t)(tick-run_last);run_last=tick;
  elapsed=(uint32_t)(run_cycles/SN_CPU_CYCLES_US);
  if(elapsed>SN_MAX_RUN_US)return sn_end(19U);
  if(!sync_mode && elapsed>=next_scan){
   if(sn_window_count>=SN_MAX_WINDOWS)return sn_end(20U);
   if(sn_configure(&sn_sync)!=0)return sn_end(21U);
   sn_window_start[sn_window_count]=dwt_readsystimestamphi32();
   sync_mode=true;scan_start=elapsed;next_scan+=SN_SCAN_INTERVAL_US;
  }
  if(sync_mode && ((first_anchor && elapsed-scan_start>=SN_SCAN_MAX_US)||(!first_anchor && elapsed>=SN_INITIAL_SCAN_MAX_US))){
   if(!first_anchor)return sn_end(22U);
   sn_window_end[sn_window_count]=dwt_readsystimestamphi32();sn_window_count++;
   if(sn_configure(&sn_aux)!=0)return sn_end(23U);
   sync_mode=false;
   continue;
  }
  status=dwt_readsysstatuslo();
  if(status==0xffffffffUL)return sn_end(24U);
  if(!(status&(DWT_INT_RXFCG_BIT_MASK|SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR)))continue;
  hi=dwt_readsysstatushi();
  if(hi==0xffffffffUL||(status&DWT_INT_SPICRCE_BIT_MASK)||(hi&SN_HI_FAULT_MASK))return sn_end(25U);
  if(!(status&DWT_INT_RXFCG_BIT_MASK)){
   sn_rx_errors++;
   if(sn_rearm()!=0)return sn_end(26U);
   continue;
  }
  len=dwt_getframelength(0);
  rf=dwt_readrxtimestamphi32();
  if(len==0U||len>sizeof(frame)){
   if(sync_mode)sn_beacon_bad++;else sn_aux_bad++;
   if(sn_rearm()!=0)return sn_end(27U);
   continue;
  }
  dwt_readrxdata(frame,len,0U);
  if(sync_mode){
   if(len==BRRS_BEACON_PSDU_BYTES&&frame[0]==0xc5U&&frame[1]==0x8cU&&frame[BRRS_IDX_MSG_TYPE]==1U&&frame[BRRS_IDX_PROTOCOL_VERSION]==BRRS_PROTOCOL_VERSION){
    uint16_t seq=brrs_get_u16_le(&frame[BRRS_IDX_SUPERFRAME_SEQ]);
    if(seq==0U||seq>2000U||sn_beacon_count>=SN_MAX_BEACONS)return sn_end(28U);
    sn_beacon_records[sn_beacon_count].rf=rf;sn_beacon_records[sn_beacon_count].seq=seq;sn_beacon_count++;
    if(!first_anchor){sn_initial_anchor=seq;first_anchor=true;}
    sn_last_anchor=seq;
    sn_window_end[sn_window_count]=dwt_readsystimestamphi32();sn_window_count++;
    if(sn_configure(&sn_aux)!=0)return sn_end(29U);
    sync_mode=false;
   }else{sn_beacon_bad++;if(sn_rearm()!=0)return sn_end(30U);}
  }else{
   if(len==26U&&frame[0]==0x41U&&frame[1]==0x88U&&frame[7]==0xa8U&&frame[8]==0x00U&&frame[9]=='B'&&frame[10]=='R'){
    uint16_t seq=(uint16_t)((uint16_t)frame[2]|((uint16_t)frame[17]<<8));
    if(sn_aux_count>=SN_MAX_AUX)return sn_end(31U);
    sn_aux_records[sn_aux_count].rf=rf;sn_aux_records[sn_aux_count].seq=seq;sn_aux_count++;
    if(sn_aux_count==1U)sn_first_aux_seq=seq;
    sn_last_aux_seq=seq;
   }else sn_aux_bad++;
   if(sn_rearm()!=0)return sn_end(32U);
  }
  if(sn_spi_bad())return sn_end(16U);
 }
}
#endif
