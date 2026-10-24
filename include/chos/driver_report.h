#ifndef CHOS_DRIVER_REPORT_H
#define CHOS_DRIVER_REPORT_H
#define CHOS_DRIVER_REPORT_ROWS 18u
#define CHOS_DRIVER_REPORT_COLS 68u
typedef unsigned (*chos_driver_report_fn)(unsigned page,char lines[CHOS_DRIVER_REPORT_ROWS][CHOS_DRIVER_REPORT_COLS+1]);
unsigned chos_platform_driver_report(unsigned page,char lines[CHOS_DRIVER_REPORT_ROWS][CHOS_DRIVER_REPORT_COLS+1]);
int chos_platform_needs_input_recovery(void);
#endif
