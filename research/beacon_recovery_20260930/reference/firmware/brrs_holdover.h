/* Isolated diagnostic only. Timestamp unit: DW high32 (~4.006 ns).
 * Lease: one immutable 2000-SF run, one owned slot, fixed 10 ms schedule.
 * Never extrapolate before training, beyond one missing SF, or beyond END.
 */
#ifndef BRRS_HOLDOVER_H
#define BRRS_HOLDOVER_H
#include <stdint.h>
#include <stdbool.h>
#ifndef BRRS_HO_MODE
#define BRRS_HO_MODE 0 /* 0=shadow, 1=holdover, 2=deterministic rejection */
#endif
volatile const uint32_t brrs_ho_mode=BRRS_HO_MODE;
#define HO_TRAIN 8U
#define HO_PERIOD 2496000UL
#define HO_SPREAD 62U
#define HO_ERROR 125U
struct ho_state {uint32_t last, period, delta[HO_TRAIN], n, pos, max_error; uint16_t seq; bool used;};
static struct ho_state ho;
static uint32_t ho_accepted, ho_attempts, ho_success, ho_injected, ho_blocked, ho_system_errors;
static uint32_t ho_rx_scheduled, ho_rx_late, ho_prediction_checks, ho_prediction_bad;
static uint16_t ho_last_dispatch;
static uint32_t ho_abs(int32_t x) {return x<0 ? (uint32_t)(-(int64_t)x) : (uint32_t)x;}
static bool ho_ready(void) {
 uint32_t lo=0xffffffffU,hi=0;
 if(ho.n<HO_TRAIN || ho.used) return false;
 for(unsigned i=0;i<HO_TRAIN;i++){if(ho.delta[i]<lo)lo=ho.delta[i];if(ho.delta[i]>hi)hi=ho.delta[i];}
 return hi-lo<=HO_SPREAD;
}
static void ho_observe(uint16_t seq,uint32_t stamp) {
 if(ho.seq && seq>ho.seq) {
  uint32_t gap=seq-ho.seq;
  if(ho.n>=HO_TRAIN) {
   uint32_t e=ho_abs((int32_t)(stamp-(ho.last+gap*ho.period)));
   ho_prediction_checks++; if(e>ho.max_error)ho.max_error=e;
   if(e>HO_ERROR){ho_prediction_bad++;ho.n=0;ho.pos=0;}
  }
  if(gap==1U) {
   uint32_t d=stamp-ho.last;
   if(ho_abs((int32_t)(d-HO_PERIOD))<=HO_PERIOD/1000U) {
    ho.delta[ho.pos++%HO_TRAIN]=d;if(ho.n<HO_TRAIN)ho.n++;
    uint64_t sum=0;for(unsigned i=0;i<ho.n;i++)sum+=ho.delta[i];ho.period=(uint32_t)(sum/ho.n);
   }else{ho.n=0;ho.pos=0;}
  }else{ho.n=0;ho.pos=0;}
 }
 ho.seq=seq;ho.last=stamp;ho.used=false;
}
static bool ho_due(uint32_t now,uint16_t limit) {
 return BRRS_HO_MODE!=0 && ho_ready() && ho.seq<limit && ho_last_dispatch<=ho.seq &&
  (int32_t)(now-(ho.last+ho.period+37440U))>=0 &&
  (int32_t)(now-(ho.last+ho.period+124800U))<0;
}
struct ho_row {uint32_t actual,predicted;uint16_t seq,kind;};
static struct ho_row ho_rows[2000];
static uint32_t ho_count;
static void ho_record(uint16_t seq,uint32_t actual,uint32_t predicted,uint16_t kind) {
 if(ho_count>=2000){ho_system_errors++;return;}
 ho_rows[ho_count++]=(struct ho_row){actual,predicted,seq,kind};
}
#endif
