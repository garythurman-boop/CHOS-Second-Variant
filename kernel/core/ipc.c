#include "chos/ipc.h"
#include "chos/syscall.h"
#include "chos/spinlock.h"
typedef struct {uint16_t size;uint8_t bytes[CHOS_IPC_MESSAGE_MAX];} message_t;
typedef struct {message_t messages[CHOS_IPC_QUEUE_DEPTH];unsigned head,count;} queue_t;
typedef struct {
 uint64_t generation;
 uint32_t owners[2];unsigned open[2],closed[2];
 uint64_t sent[2],received[2];queue_t queues[2];
} channel_t;
static channel_t channels[CHOS_IPC_CHANNELS];
static uint64_t next_generation=1;
static chos_spinlock_t lock=CHOS_SPINLOCK_INITIALIZER;
static void erase(void *memory,size_t n){volatile uint8_t *p=memory;while(n--)*p++=0;}
static uint64_t handle(unsigned slot,unsigned side){return (channels[slot].generation<<8)|(slot*2u+side+1u);}
static channel_t *lookup(uint32_t owner,uint64_t h,unsigned *side){
 unsigned n=(unsigned)(h&255u);if(!owner||!n||n>2u*CHOS_IPC_CHANNELS)return NULL;
 unsigned index=(n-1u)/2u,s=(n-1u)%2u;channel_t *c=&channels[index];
 if(!c->generation||!c->open[s]||c->owners[s]!=owner||handle(index,s)!=h)return NULL;
 *side=s;return c;
}
static void close_side(channel_t *c,unsigned side){
 c->open[side]=0;c->closed[side]=1;erase(&c->queues[side],sizeof(c->queues[side]));
 if(!c->open[0]&&!c->open[1])erase(c,sizeof(*c));
}
int64_t chos_ipc_open(uint32_t owner,uint32_t peer){
 if(!owner||!peer||owner==peer)return -CHOS_ERRNO_EINVAL;
 uint64_t f=chos_spin_lock_irqsave(&lock);int64_t result=-CHOS_ERRNO_ENOSPC;unsigned owned=0,free_slot=CHOS_IPC_CHANNELS;
 for(unsigned i=0;i<CHOS_IPC_CHANNELS;i++)for(unsigned j=0;j<2;j++)
  if(channels[i].generation&&channels[i].owners[j]==owner&&channels[i].open[j])++owned;
 for(unsigned i=0;i<CHOS_IPC_CHANNELS;i++){
  channel_t *c=&channels[i];if(!c->generation){if(free_slot==CHOS_IPC_CHANNELS)free_slot=i;continue;}
  for(unsigned side=0;side<2;side++){
   if(c->owners[side]!=owner)continue;
   if(c->owners[side^1u]==peer){
    if(c->closed[side]||c->closed[side^1u])result=-CHOS_ERRNO_EBUSY;
    else if(!c->open[side]&&owned>=CHOS_IPC_OWNER_LIMIT)result=-CHOS_ERRNO_EMFILE;
    else {c->open[side]=1;result=(int64_t)handle(i,side);}
    goto done;
   }
  }
 }
 if(owned>=CHOS_IPC_OWNER_LIMIT){result=-CHOS_ERRNO_EMFILE;goto done;}
 /* Never wrap generation numbers into a previously issued handle. */
 if(next_generation>UINT64_C(0x007fffffffffffff)){result=-CHOS_ERRNO_ENOSPC;goto done;}
 if(free_slot<CHOS_IPC_CHANNELS){
  channel_t *c=&channels[free_slot];c->generation=next_generation++;
  c->owners[0]=owner;c->owners[1]=peer;c->open[0]=1;
  result=(int64_t)handle(free_slot,0);
 }
 done:chos_spin_unlock_irqrestore(&lock,f);return result;
}
int chos_ipc_status(uint32_t owner,uint64_t h,chos_ipc_info_t *out){
 if(!out)return -CHOS_ERRNO_EINVAL;
 uint64_t f=chos_spin_lock_irqsave(&lock);unsigned s;channel_t *c=lookup(owner,h,&s);int rc=-CHOS_ERRNO_EBADF;
 if(c){queue_t *q=&c->queues[s];*out=(chos_ipc_info_t){.version=1,.size_bytes=sizeof(*out),.handle=h,
  .peer_pid=c->owners[s^1u],.flags=c->closed[s^1u]?CHOS_IPC_PEER_CLOSED:(c->open[s^1u]?CHOS_IPC_CONNECTED:0),
  .queued_messages=q->count,.next_message_bytes=q->count?q->messages[q->head].size:0,
  .sent_bytes=c->sent[s],.received_bytes=c->received[s]};rc=0;}
 chos_spin_unlock_irqrestore(&lock,f);return rc;
}
int chos_ipc_send(uint32_t owner,uint64_t h,const void *bytes,size_t n){
 if(!bytes||!n||n>CHOS_IPC_MESSAGE_MAX)return -CHOS_ERRNO_EINVAL;
 uint64_t f=chos_spin_lock_irqsave(&lock);unsigned s;channel_t *c=lookup(owner,h,&s);int rc=-CHOS_ERRNO_EBADF;
 if(c){queue_t *q=&c->queues[s^1u];
  if(c->closed[s^1u])rc=-CHOS_ERRNO_EPIPE;
  else if(!c->open[s^1u]||q->count==CHOS_IPC_QUEUE_DEPTH)rc=-CHOS_ERRNO_EAGAIN;
  else{message_t *m=&q->messages[(q->head+q->count)%CHOS_IPC_QUEUE_DEPTH];
   for(size_t i=0;i<n;i++)m->bytes[i]=((const uint8_t *)bytes)[i];m->size=(uint16_t)n;++q->count;
   c->sent[s]+=n;rc=(int)n;}
 }
 chos_spin_unlock_irqrestore(&lock,f);return rc;
}
int chos_ipc_receive(uint32_t owner,uint64_t h,void *bytes,size_t n){
 if(!bytes||!n||n>CHOS_IPC_MESSAGE_MAX)return -CHOS_ERRNO_EINVAL;
 uint64_t f=chos_spin_lock_irqsave(&lock);unsigned s;channel_t *c=lookup(owner,h,&s);int rc=-CHOS_ERRNO_EBADF;
 if(c){queue_t *q=&c->queues[s];
  if(!q->count)rc=c->closed[s^1u]?0:-CHOS_ERRNO_EAGAIN;
  else{message_t *m=&q->messages[q->head];if(m->size>n)rc=-CHOS_ERRNO_EMSGSIZE;
   else{rc=m->size;for(unsigned i=0;i<m->size;i++)((uint8_t *)bytes)[i]=m->bytes[i];
    c->received[s]+=m->size;erase(m,sizeof(*m));q->head=(q->head+1u)%CHOS_IPC_QUEUE_DEPTH;--q->count;}
  }
 }
 chos_spin_unlock_irqrestore(&lock,f);return rc;
}
int chos_ipc_close(uint32_t owner,uint64_t h){
 uint64_t f=chos_spin_lock_irqsave(&lock);unsigned s;channel_t *c=lookup(owner,h,&s);int rc=-CHOS_ERRNO_EBADF;
 if(c){close_side(c,s);rc=0;}chos_spin_unlock_irqrestore(&lock,f);return rc;
}
void chos_ipc_drop_owner(uint32_t owner){
 if(!owner)return;
 uint64_t f=chos_spin_lock_irqsave(&lock);
 for(unsigned i=0;i<CHOS_IPC_CHANNELS;i++)for(unsigned s=0;s<2;s++)
  if(channels[i].generation&&channels[i].owners[s]==owner)close_side(&channels[i],s);
 chos_spin_unlock_irqrestore(&lock,f);
}
