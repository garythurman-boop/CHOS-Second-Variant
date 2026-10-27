/* Keep parser exports separate from host libc and sanitizer interceptors. */
#define strtof chos_scan_strtof
#define strtod chos_scan_strtod
#define strtold chos_scan_strtold
#define atof chos_scan_atof
#define __floatscan chos_scan_floatscan
#define __shlim chos_scan_shlim
#define __shgetc chos_scan_shgetc
