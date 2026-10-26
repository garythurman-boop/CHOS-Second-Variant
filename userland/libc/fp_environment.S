.text
.balign 4
.global __chos_pthread_get_fp
.type __chos_pthread_get_fp,%function
__chos_pthread_get_fp:
    mrs x2,fpcr
    mrs x3,fpsr
    str x2,[x0]
    str x3,[x1]
    ret
.size __chos_pthread_get_fp,.-__chos_pthread_get_fp
.global __chos_pthread_set_fp
.type __chos_pthread_set_fp,%function
__chos_pthread_set_fp:
    msr fpcr,x0
    msr fpsr,x1
    isb
    ret
.size __chos_pthread_set_fp,.-__chos_pthread_set_fp
