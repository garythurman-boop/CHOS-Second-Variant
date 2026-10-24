#include "chos/xhci.h"
_Static_assert(sizeof(chos_xhci_trb_t)==16,"xHCI TRB ABI");
void chos_xhci_ring_init(chos_xhci_ring_t *r) {
    for (unsigned i=0;i<CHOS_XHCI_RING_TRBS;++i) r->trbs[i]=(chos_xhci_trb_t){0,0,0};
    r->enqueue=0; r->cycle=1;
}
uint64_t chos_xhci_ring_push(chos_xhci_ring_t *r, uint64_t p, uint32_t s, uint32_t c) {
    unsigned i;
    if (!r || r->enqueue>=CHOS_XHCI_RING_TRBS-1 || r->cycle>1) return 0;
    i=r->enqueue++;
    r->trbs[i]=(chos_xhci_trb_t){p,s,(c&~1u)|r->cycle};
    if (r->enqueue==CHOS_XHCI_RING_TRBS-1) {
        r->trbs[r->enqueue]=(chos_xhci_trb_t){(uintptr_t)r->trbs,0,(6u<<10)|2u|r->cycle};
        r->enqueue=0; r->cycle^=1;
    }
    return (uintptr_t)&r->trbs[i];
}
