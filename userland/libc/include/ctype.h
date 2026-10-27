#ifndef CHOS_LIBC_CTYPE_H
#define CHOS_LIBC_CTYPE_H
/* Byte classification in the native ASCII-compatible C locale. Unicode
 * code-point properties belong to the Unicode/ICU layer, not UTF-8 bytes. */
#ifdef __cplusplus
extern "C" {
#endif
int isalnum(int);int isalpha(int);int isblank(int);int iscntrl(int);
int isdigit(int);int isgraph(int);int islower(int);int isprint(int);
int ispunct(int);int isspace(int);int isupper(int);int isxdigit(int);
int tolower(int);int toupper(int);int isascii(int);int toascii(int);
#ifdef __cplusplus
}
#endif
#endif
