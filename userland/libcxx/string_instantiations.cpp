/* Instantiate the real upstream ABI-v1 char-string member list only.
 * Numeric string conversions/formatting and locale are separate open work. */
#include <string>
_LIBCPP_BEGIN_NAMESPACE_STD
#define CHOS_DEFINE(...) template __VA_ARGS__;
_LIBCPP_STRING_V1_EXTERN_TEMPLATE_LIST(CHOS_DEFINE, char)
#undef CHOS_DEFINE
template string operator+<char,char_traits<char>,allocator<char>>(char const*,string const&);
_LIBCPP_END_NAMESPACE_STD
